/**
 * openextraflame - Micronova master polling loop
 *
 * ARCHITECTURE (=confirmée par exécution du firmware Extraflame original sur
 * M5Stack Atom Lite le 2026-07-07):
 * Le module ESP32 est MASTER Micronova. Il interroge le poêle en cyclique.
 * Le poêle répond aux requêtes.
 *
 * PROTOCOLE MICRONOVA (=frame layout observé) :
 *   Read  : Master → [0x00, addr]              (2 octets)
 *           Slave  ← [addr, value, ~checksum]  (3 octets)
 *             checksum = value ; verify: rx[0]==addr && rx[2]==(uint8_t)~rx[1]
 *
 *   Write : Master → [0x80|addr, value, ~value] (3 octets)
 *           Slave  ← [value, ~value]            (2 octets confirm)
 *
 * Shadow RAM: le master task remplit le shadow à chaque READ reply.
 * Commandes MQTT enqueue des writes qui sont émis entre deux polls.
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "micronova.h"
#include "hardware_config.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "esp_timer.h"

static const char *TAG = "MICRONOVA";

#define MN_RX_BUFFER_SIZE   512
#define MN_TX_BUFFER_SIZE   256
#define MN_TIMEOUT_MS       1000

extern EventGroupHandle_t app_event_group;
extern const int STOVE_ONLINE_BIT;

static SemaphoreHandle_t mn_mutex;
static mn_stove_state_snapshot_t mn_snapshot;

/* Sniffer mode - when true the master task stops polling so /debug/uart-sniffer
 * has an exclusive read on the UART for the requested duration. */
static volatile bool mn_sniffer_pause = false;

/* Shadow RAM : miroir des registres Micronova.
 * Ecrit par MQTT commands, lu par le poêle.
 */
static uint8_t mn_ram_shadow[MN_RAM_MAX];

esp_err_t mn_set_ram(mn_ram_addr_t addr, uint8_t value)
{
    if (addr >= MN_RAM_MAX) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    mn_ram_shadow[addr] = value;
    xSemaphoreGive(mn_mutex);
    ESP_LOGD(TAG, "shadow[0x%02x] = 0x%02x", addr, value);
    return ESP_OK;
}

/* Forward decl - defined below in the master-task block. */
static void mn_enqueue_write(uint16_t addr, uint8_t value);

esp_err_t mn_write_register(mn_ram_addr_t addr, uint8_t value)
{
    if (addr >= MN_RAM_MAX) return ESP_ERR_INVALID_ARG;
    mn_set_ram(addr, value);
    mn_enqueue_write((uint16_t)addr, value);
    ESP_LOGI(TAG, "queued write [0x%02x] = 0x%02x", addr, value);
    return ESP_OK;
}

uint8_t mn_get_ram(mn_ram_addr_t addr)
{
    if (addr >= MN_RAM_MAX) return 0;
    uint8_t v = 0;
    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        v = mn_ram_shadow[addr];
        xSemaphoreGive(mn_mutex);
    }
    return v;
}

void mn_get_snapshot(mn_stove_state_snapshot_t *out)
{
    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        memcpy(out, &mn_snapshot, sizeof(mn_snapshot));
        xSemaphoreGive(mn_mutex);
    }
}

/* Mise à jour snapshot depuis shadow RAM après chaque frame reçue */
static void update_snapshot_from_shadow(void)
{
    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    mn_snapshot.state           = (mn_stove_state_t)mn_ram_shadow[MN_RAM_STOVE_STATUS];
    mn_snapshot.power_level     = mn_ram_shadow[MN_RAM_POT_REALE];
    mn_snapshot.alarm_code      = mn_ram_shadow[MN_RAM_ALLARM];
    mn_snapshot.t_ambient       = mn_ram_shadow[MN_RAM_TAMB];
    mn_snapshot.t_water         = mn_ram_shadow[MN_RAM_TH20];
    mn_snapshot.t_smoke         = mn_ram_shadow[MN_RAM_T_FUMI];
    mn_snapshot.t_chamber       = mn_ram_shadow[MN_RAM_T_CAMERA];
    mn_snapshot.last_updated_ms = (uint32_t)(esp_timer_get_time() / 1000);
    xSemaphoreGive(mn_mutex);
}

/* Master role : poll the stove periodically.
 *
 * Protocol (=philibertc/micronova_controller + ridiculouslab):
 *   Read  : Master sends [0x00, addr]           Slave replies [checksum, value]
 *                                              checksum = (addr + value) & 0xFF
 *   Write : Master sends [0x80|addr, value]     Slave replies [checksum, value]
 *                                              (Reply confirms the write.)
 *
 * We keep a small ring of pending write commands populated by mn_set_ram()
 * from the MQTT layer, and interleave them with cyclic polls of the
 * registers HA cares about.
 */
/* Poll list is mutable at runtime via /debug/poll-list.
 * Contains up to 32 register addresses (=uint16 to allow bank 1). */
#define POLL_LIST_MAX 32
static uint16_t poll_list[POLL_LIST_MAX] = {
    MN_RAM_STOVE_STATUS,   /* 0xD1 */
    MN_RAM_TAMB,           /* 0xD3 */
    MN_RAM_TH20,           /* 0xD0 */
    MN_RAM_T_FUMI,         /* 0xD9 */
    MN_RAM_POT_REALE,      /* 0xD8 */
    MN_RAM_ALLARM,         /* 0xD2 */
    MN_RAM_STATO_GESTITO,  /* 0xD5 */
    MN_RAM_RESET_UTENTE,   /* 0xD4 */
};
static int poll_list_count = 8;
static int poll_interval_ms = 150;   /* delay between two polls */

/* Pending write queue - mn_set_ram() enqueues, master task drains. */
typedef struct { uint16_t addr; uint8_t value; } mn_write_cmd_t;
#define WRITE_Q_SIZE 8
static mn_write_cmd_t write_q[WRITE_Q_SIZE];
static int write_q_head = 0;
static int write_q_tail = 0;

static void mn_enqueue_write(uint16_t addr, uint8_t value)
{
    int next = (write_q_head + 1) % WRITE_Q_SIZE;
    if (next == write_q_tail) return;  /* queue full, drop */
    write_q[write_q_head].addr  = addr;
    write_q[write_q_head].value = value;
    write_q_head = next;
}

static bool mn_dequeue_write(mn_write_cmd_t *out)
{
    if (write_q_head == write_q_tail) return false;
    *out = write_q[write_q_tail];
    write_q_tail = (write_q_tail + 1) % WRITE_Q_SIZE;
    return true;
}

/* Send one Micronova RWMS transaction and update the RAM shadow.
 *
 * PROTOCOLE reversé du firmware navel (rwms_master_read @ 0x400e4df4) :
 *
 *   Requête : [loc, addr]   (2 octets)
 *     loc = (source << 5) | (bank & 0x1F)
 *       source: 0 = RAM, 1 = EEPROM
 *       bank  : 0 = page normale, 1 = page étendue (bits 0..4)
 *     addr = octet bas de l'adresse
 *
 *   Réponse : [checksum, value]   (2 octets)
 *     checksum = (loc + addr + value) & 0xFF   (=ADDITIF, PAS complément)
 *
 *   Écho : le poêle peut d'abord ré-émettre [loc, addr] avant la vraie
 *          réponse. On skip l'écho.
 *
 * Pour un write, la structure est symétrique (=bit 7 du loc levé, valeur
 * ajoutée à la requête). Nous implémentons le read maintenant, le write
 * suit le même pattern.
 */
static bool mn_do_transaction(uint8_t cmd, uint16_t reg_addr, uint8_t value_to_write)
{
    bool is_write = (cmd & 0x80) != 0;
    uint8_t bank  = (reg_addr >> 8) & 0x1F;
    uint8_t addr  = reg_addr & 0xFF;
    uint8_t loc   = bank;                    /* source=0 (RAM), source_bit_5=0 */
    if (is_write) loc |= 0x80;

    /* WRITE = 4 octets [loc addr value checksum_additif]
     * READ  = 2 octets [loc addr] */
    uint8_t tx[4];
    int tx_len;
    if (is_write) {
        tx[0] = loc;
        tx[1] = addr;
        tx[2] = value_to_write;
        tx[3] = (uint8_t)((loc + addr + value_to_write) & 0xFF);
        tx_len = 4;
    } else {
        tx[0] = loc;
        tx[1] = addr;
        tx_len = 2;
    }

    uart_flush(STOVE_UART_NUM);
    uart_write_bytes(STOVE_UART_NUM, (const char *)tx, tx_len);
    uart_wait_tx_done(STOVE_UART_NUM, pdMS_TO_TICKS(50));

    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        mn_snapshot.tx_frames_count++;
        xSemaphoreGive(mn_mutex);
    }
    mn_debug_push(is_write ? MN_DIR_RX_WRITE : MN_DIR_RX_READ,
                  addr, is_write ? value_to_write : 0);
    if (is_write) {
        ESP_LOGI(TAG, "TX write [0x%02X 0x%02X 0x%02X 0x%02X] -> reg=0x%03X val=0x%02X cks=0x%02X",
                 tx[0], tx[1], tx[2], tx[3], reg_addr, value_to_write, tx[3]);
    } else {
        ESP_LOGI(TAG, "TX read  [0x%02X 0x%02X] -> reg=0x%03X (bank=%u)",
                 tx[0], tx[1], reg_addr, bank);
    }

    /* Lecture d'abord des 2 octets. Si les 2 premiers ressemblent à un écho
     * [loc, addr], on lit 2 octets de plus pour obtenir la vraie réponse. */
    uint8_t rx[4] = {0};
    int got = uart_read_bytes(STOVE_UART_NUM, rx, 2, pdMS_TO_TICKS(MN_TIMEOUT_MS));
    if (got != 2) {
        ESP_LOGW(TAG, "no reply reg=0x%03X (got %d/2)", reg_addr, got);
        return false;
    }

    if (rx[0] == loc && rx[1] == addr) {
        ESP_LOGI(TAG, "RX echo detected [0x%02X 0x%02X], reading real reply",
                 rx[0], rx[1]);
        int got2 = uart_read_bytes(STOVE_UART_NUM, rx, 2, pdMS_TO_TICKS(MN_TIMEOUT_MS));
        if (got2 != 2) {
            ESP_LOGW(TAG, "no reply after echo reg=0x%03X (got %d/2)", reg_addr, got2);
            return false;
        }
    }

    uint8_t cksum = rx[0];
    uint8_t value = rx[1];
    uint8_t expected_cksum = (uint8_t)((loc + addr + value) & 0xFF);
    ESP_LOGI(TAG, "RX reply [cks=0x%02X val=0x%02X] reg=0x%03X (expected cks=0x%02X)",
             cksum, value, reg_addr, expected_cksum);

    if (cksum != expected_cksum) {
        ESP_LOGW(TAG, "bad additive cksum reg=0x%03X: got 0x%02X expected 0x%02X (val=0x%02X)",
                 reg_addr, cksum, expected_cksum, value);
        /* Not fatal - still record value for debugging */
    }

    if (reg_addr < MN_RAM_MAX) {
        mn_set_ram((mn_ram_addr_t)reg_addr, value);
    }
    mn_debug_push(MN_DIR_TX_REPLY, addr, value);

    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        mn_snapshot.rx_frames_count++;
        mn_snapshot.online = true;
        xSemaphoreGive(mn_mutex);
    }
    xEventGroupSetBits(app_event_group, STOVE_ONLINE_BIT);
    return true;
}

static void mn_slave_task(void *arg)
{
    ESP_LOGI(TAG, "Master polling loop started on UART%d", STOVE_UART_NUM);
    size_t poll_idx = 0;

    for (;;) {
        /* If a sniffer session is active, yield the UART entirely. */
        if (mn_sniffer_pause) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* Drain any queued writes first (=user commands) */
        mn_write_cmd_t w;
        while (mn_dequeue_write(&w)) {
            mn_do_transaction(0x80 | w.addr, w.addr, w.value);
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        /* Then one cyclic read */
        if (poll_list_count > 0) {
            uint16_t addr = poll_list[poll_idx % poll_list_count];
            mn_do_transaction(0x00, addr, 0);
            poll_idx = (poll_idx + 1) % poll_list_count;
        }

        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
    }
}

/* Public API: capture raw UART bytes for duration_ms with the master task paused.
 * Returns heap-allocated JSON:
 *   { "duration_ms": N, "bytes_read": M, "hex": "AA BB CC ...", "ascii": "..." }
 * Also emits a single "hello ping" 0x00 0x21 mid-window so we can see if the
 * stove replies to a specific stimulus without our master task's noise.
 * Caller frees the returned string. */
char *mn_sniffer_capture(int duration_ms)
{
    if (duration_ms < 100) duration_ms = 100;
    if (duration_ms > 15000) duration_ms = 15000;

    mn_sniffer_pause = true;
    vTaskDelay(pdMS_TO_TICKS(250));  /* let the master task settle */
    uart_flush(STOVE_UART_NUM);

    uint8_t buf[512];
    size_t total = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t end_us   = start_us + (int64_t)duration_ms * 1000;

    /* Fire a single ping halfway through the window. */
    bool ping_sent = false;
    int64_t ping_at = start_us + (int64_t)(duration_ms * 500);

    while (esp_timer_get_time() < end_us) {
        int got = uart_read_bytes(STOVE_UART_NUM, buf + total,
                                  sizeof(buf) - total,
                                  pdMS_TO_TICKS(50));
        if (got > 0) total += got;
        if (total >= sizeof(buf)) break;

        if (!ping_sent && esp_timer_get_time() >= ping_at) {
            uint8_t ping[2] = { 0x00, 0x21 };  /* Micronova read STOVE_STATUS */
            uart_write_bytes(STOVE_UART_NUM, (const char *)ping, sizeof(ping));
            uart_wait_tx_done(STOVE_UART_NUM, pdMS_TO_TICKS(50));
            ping_sent = true;
        }
    }

    mn_sniffer_pause = false;

    /* Build hex + ascii */
    size_t hex_len = total * 3 + 1;
    char *hex = malloc(hex_len);
    char *asc = malloc(total + 1);
    if (!hex || !asc) { free(hex); free(asc); return NULL; }
    hex[0] = 0;
    for (size_t i = 0; i < total; i++) {
        char tmp[4];
        snprintf(tmp, sizeof(tmp), "%02X ", buf[i]);
        strcat(hex, tmp);
        asc[i] = (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
    }
    asc[total] = 0;

    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "duration_ms", duration_ms);
    cJSON_AddNumberToObject(o, "bytes_read", (double)total);
    cJSON_AddBoolToObject  (o, "ping_sent", ping_sent);
    cJSON_AddStringToObject(o, "ping_hex", "00 21");
    cJSON_AddStringToObject(o, "hex", hex);
    cJSON_AddStringToObject(o, "ascii", asc);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    free(hex);
    free(asc);
    return s;
}

/* Parse a space/comma-separated hex string into a byte buffer.
 * Accepts "20 00", "20,00", "0x20 0x00", "2000" (no separator).
 * Returns number of bytes parsed, or -1 on error. */
static int parse_hex_bytes(const char *s, uint8_t *out, int out_max)
{
    int n = 0;
    while (*s && n < out_max) {
        while (*s && (*s == ' ' || *s == ',' || *s == '\t')) s++;
        if (!*s) break;
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
        if (!isxdigit((unsigned char)s[0])) return -1;
        char buf[3] = { s[0], 0, 0 };
        if (isxdigit((unsigned char)s[1])) { buf[1] = s[1]; s += 2; } else { s += 1; }
        out[n++] = (uint8_t)strtoul(buf, NULL, 16);
    }
    return n;
}

/* SLIP encode a payload into out. Returns encoded length. */
static int slip_encode(const uint8_t *in, int in_len, uint8_t *out, int out_max)
{
    int o = 0;
    if (o < out_max) out[o++] = 0xC0;      /* leading END */
    for (int i = 0; i < in_len; i++) {
        if (o + 1 >= out_max) break;
        if (in[i] == 0xC0)      { out[o++] = 0xDB; out[o++] = 0xDC; }
        else if (in[i] == 0xDB) { out[o++] = 0xDB; out[o++] = 0xDD; }
        else                    { out[o++] = in[i]; }
    }
    if (o < out_max) out[o++] = 0xC0;      /* trailing END */
    return o;
}

char *mn_raw_tx(const char *tx_hex, int timeout_ms)
{
    return mn_raw_tx_ex(tx_hex, timeout_ms, false);
}

char *mn_raw_tx_ex(const char *tx_hex, int timeout_ms, bool wrap_slip)
{
    if (!tx_hex) return NULL;
    if (timeout_ms < 50) timeout_ms = 50;
    if (timeout_ms > 5000) timeout_ms = 5000;

    uint8_t tx[64];
    int tx_len = parse_hex_bytes(tx_hex, tx, sizeof(tx));
    if (tx_len <= 0) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddBoolToObject(e, "ok", false);
        cJSON_AddStringToObject(e, "error", "bad hex input");
        char *s = cJSON_PrintUnformatted(e);
        cJSON_Delete(e);
        return s;
    }

    mn_sniffer_pause = true;
    vTaskDelay(pdMS_TO_TICKS(150));
    uart_flush(STOVE_UART_NUM);

    uint8_t tx_wire[130];
    int tx_wire_len;
    if (wrap_slip) {
        tx_wire_len = slip_encode(tx, tx_len, tx_wire, sizeof(tx_wire));
    } else {
        tx_wire_len = tx_len;
        memcpy(tx_wire, tx, tx_len);
    }
    uart_write_bytes(STOVE_UART_NUM, (const char *)tx_wire, tx_wire_len);
    uart_wait_tx_done(STOVE_UART_NUM, pdMS_TO_TICKS(100));

    uint8_t rx[128];
    int rx_len = uart_read_bytes(STOVE_UART_NUM, rx, sizeof(rx),
                                 pdMS_TO_TICKS(timeout_ms));
    if (rx_len < 0) rx_len = 0;

    mn_sniffer_pause = false;

    char hex_out[3 * 128 + 1] = {0};
    for (int i = 0; i < rx_len; i++) {
        char t[4]; snprintf(t, sizeof(t), "%02X ", rx[i]);
        strcat(hex_out, t);
    }
    char hex_in[3 * 64 + 1] = {0};
    for (int i = 0; i < tx_len; i++) {
        char t[4]; snprintf(t, sizeof(t), "%02X ", tx[i]);
        strcat(hex_in, t);
    }

    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject  (o, "ok", true);
    cJSON_AddStringToObject(o, "tx", hex_in);
    cJSON_AddNumberToObject(o, "tx_len", tx_len);
    cJSON_AddStringToObject(o, "rx", hex_out);
    cJSON_AddNumberToObject(o, "rx_len", rx_len);
    cJSON_AddNumberToObject(o, "timeout_ms", timeout_ms);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

char *mn_tx_self_check(void)
{
    mn_sniffer_pause = true;
    vTaskDelay(pdMS_TO_TICKS(150));

    /* Start a background sampling of GPIO23 at maximum speed while we
     * send a burst. The pin remains muxed to UART TX; we just read its
     * live level via gpio_get_level. */
    uint32_t samples = 0, transitions = 0, high_count = 0;
    int64_t start_us = esp_timer_get_time();

    /* Fire a 50-byte 0x55 burst (=alternating bits, plenty of transitions
     * at 38400 baud = ~13ms). */
    uint8_t burst[50];
    memset(burst, 0x55, sizeof(burst));
    uart_write_bytes(STOVE_UART_NUM, (const char *)burst, sizeof(burst));

    /* Sample during the burst - ~30ms window covers the 13ms actual TX
     * plus post-TX idle. */
    int64_t end_us = start_us + 30000;
    int prev = gpio_get_level((gpio_num_t)STOVE_UART_TX_PIN);
    while (esp_timer_get_time() < end_us) {
        int lvl = gpio_get_level((gpio_num_t)STOVE_UART_TX_PIN);
        if (lvl != prev) { transitions++; prev = lvl; }
        if (lvl) high_count++;
        samples++;
    }
    uart_wait_tx_done(STOVE_UART_NUM, pdMS_TO_TICKS(50));

    int duty_pct = samples > 0 ? (int)((high_count * 100) / samples) : 0;

    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "samples",     samples);
    cJSON_AddNumberToObject(o, "transitions", transitions);
    cJSON_AddNumberToObject(o, "high_pct",    duty_pct);
    cJSON_AddNumberToObject(o, "burst_bytes", (int)sizeof(burst));
    const char *diag;
    if (transitions < 10) {
        diag = "TX pin STUCK - line not pulsing (=TX driver failed?)";
    } else {
        diag = "TX pin pulses OK - signal physically driven";
    }
    cJSON_AddStringToObject(o, "diagnosis", diag);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);

    mn_sniffer_pause = false;
    return s;
}

char *mn_rx_raw_sample(int duration_ms)
{
    if (duration_ms < 50)   duration_ms = 50;
    if (duration_ms > 2000) duration_ms = 2000;

    mn_sniffer_pause = true;
    vTaskDelay(pdMS_TO_TICKS(150));

    /* Detach GPIO5 from UART temporarily and use as GPIO input. */
    gpio_reset_pin((gpio_num_t)STOVE_UART_RX_PIN);
    gpio_set_direction((gpio_num_t)STOVE_UART_RX_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en((gpio_num_t)STOVE_UART_RX_PIN);

    /* Tight loop sampling. Target ~100kHz = 10us/sample. */
    int64_t start_us = esp_timer_get_time();
    int64_t end_us   = start_us + (int64_t)duration_ms * 1000;

    uint32_t samples = 0, transitions = 0, high_count = 0;
    int prev = gpio_get_level((gpio_num_t)STOVE_UART_RX_PIN);
    int64_t first_low_us = -1, first_high_us = -1;

    while (esp_timer_get_time() < end_us) {
        int lvl = gpio_get_level((gpio_num_t)STOVE_UART_RX_PIN);
        if (lvl != prev) {
            transitions++;
            if (first_low_us < 0 && lvl == 0)  first_low_us  = esp_timer_get_time() - start_us;
            if (first_high_us < 0 && lvl == 1) first_high_us = esp_timer_get_time() - start_us;
            prev = lvl;
        }
        if (lvl) high_count++;
        samples++;
    }

    /* Restore UART on this pin */
    uart_set_pin(STOVE_UART_NUM, STOVE_UART_TX_PIN, STOVE_UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    /* Re-enable inversion (=we reset the pin so line-inverse might have been lost) */
    uart_set_line_inverse(STOVE_UART_NUM,
                          UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV);
    gpio_set_pull_mode((gpio_num_t)STOVE_UART_RX_PIN, GPIO_PULLUP_ONLY);

    int duty_pct = samples > 0 ? (int)((high_count * 100) / samples) : 0;

    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "duration_ms",   duration_ms);
    cJSON_AddNumberToObject(o, "samples",       samples);
    cJSON_AddNumberToObject(o, "transitions",   transitions);
    cJSON_AddNumberToObject(o, "high_percent",  duty_pct);
    cJSON_AddNumberToObject(o, "first_low_us",  (double)first_low_us);
    cJSON_AddNumberToObject(o, "first_high_us", (double)first_high_us);
    /* Interpretation hint */
    const char *diag;
    if (transitions == 0) {
        diag = (duty_pct >= 90) ? "line stuck HIGH (=idle, poele silent)" :
               (duty_pct <= 10) ? "line stuck LOW (=abnormal)" : "line stuck mid-level";
    } else if (transitions < 10) {
        diag = "few transitions (=some noise or single event)";
    } else {
        diag = "many transitions (=signal PRESENT, UART decoding likely wrong)";
    }
    cJSON_AddStringToObject(o, "diagnosis", diag);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);

    mn_sniffer_pause = false;
    return s;
}

char *mn_reconfig_uart(int baud, int rx_inv, int tx_inv)
{
    return mn_reconfig_uart_ex(baud, rx_inv, tx_inv, 0);
}

char *mn_reconfig_uart_ex(int baud, int rx_inv, int tx_inv, int stop_bits)
{
    mn_sniffer_pause = true;
    vTaskDelay(pdMS_TO_TICKS(200));

    if (baud >= 1200 && baud <= 460800) {
        uart_set_baudrate(STOVE_UART_NUM, baud);
    }
    if (stop_bits == 1) {
        uart_set_stop_bits(STOVE_UART_NUM, UART_STOP_BITS_1);
    } else if (stop_bits == 2) {
        uart_set_stop_bits(STOVE_UART_NUM, UART_STOP_BITS_2);
    }
    uint32_t inv_mask = 0;
    if (rx_inv > 0) inv_mask |= UART_SIGNAL_RXD_INV;
    if (tx_inv > 0) inv_mask |= UART_SIGNAL_TXD_INV;
    uart_set_line_inverse(STOVE_UART_NUM, 0);
    if (inv_mask) uart_set_line_inverse(STOVE_UART_NUM, inv_mask);

    uart_flush(STOVE_UART_NUM);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject  (o, "ok", true);
    cJSON_AddNumberToObject(o, "baud_applied", baud);
    cJSON_AddNumberToObject(o, "stop_bits_applied", stop_bits);
    cJSON_AddBoolToObject  (o, "rx_inv", rx_inv > 0);
    cJSON_AddBoolToObject  (o, "tx_inv", tx_inv > 0);
    cJSON_AddNumberToObject(o, "inv_mask", inv_mask);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);

    mn_sniffer_pause = false;
    return s;
}

char *mn_addr_scan(uint8_t opcode, uint8_t start, uint8_t end, int per_addr_ms)
{
    if (per_addr_ms < 30)  per_addr_ms = 30;
    if (per_addr_ms > 500) per_addr_ms = 500;
    if (end < start) { uint8_t t = start; start = end; end = t; }
    /* keep scan bounded so the httpd doesn't timeout */
    if ((int)end - (int)start > 128) end = start + 128;

    mn_sniffer_pause = true;
    vTaskDelay(pdMS_TO_TICKS(150));

    cJSON *o = cJSON_CreateObject();
    cJSON *hits = cJSON_CreateArray();
    int addr_tried = 0, addr_hit = 0;

    for (int a = start; a <= (int)end; a++) {
        uart_flush(STOVE_UART_NUM);
        uint8_t tx[2] = { opcode, (uint8_t)a };
        uart_write_bytes(STOVE_UART_NUM, (const char *)tx, 2);
        uart_wait_tx_done(STOVE_UART_NUM, pdMS_TO_TICKS(50));

        uint8_t rx[16];
        int got = uart_read_bytes(STOVE_UART_NUM, rx, sizeof(rx),
                                  pdMS_TO_TICKS(per_addr_ms));
        addr_tried++;
        if (got > 0) {
            addr_hit++;
            cJSON *h = cJSON_CreateObject();
            cJSON_AddNumberToObject(h, "addr", a);
            char hs[3 * 16 + 1] = {0};
            for (int i = 0; i < got; i++) {
                char t[4]; snprintf(t, sizeof(t), "%02X ", rx[i]);
                strcat(hs, t);
            }
            cJSON_AddStringToObject(h, "rx", hs);
            cJSON_AddNumberToObject(h, "rx_len", got);
            cJSON_AddItemToArray(hits, h);
        }
    }

    mn_sniffer_pause = false;

    cJSON_AddNumberToObject(o, "opcode", opcode);
    cJSON_AddNumberToObject(o, "start", start);
    cJSON_AddNumberToObject(o, "end", end);
    cJSON_AddNumberToObject(o, "per_addr_ms", per_addr_ms);
    cJSON_AddNumberToObject(o, "tried", addr_tried);
    cJSON_AddNumberToObject(o, "hits_count", addr_hit);
    cJSON_AddItemToObject   (o, "hits", hits);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

/* Legacy slave-style handler retained under #if 0 for reference.
 * The real bus wants us to be master (=rwms_master.c in the factory
 * firmware), which is now handled above. */
#if 0
static void mn_legacy_slave_body(void)
{
    for (;;) {
        uint8_t cmd_byte;
        int r = uart_read_bytes(STOVE_UART_NUM, &cmd_byte, 1, portMAX_DELAY);
        if (r != 1) continue;

        if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            mn_snapshot.rx_frames_count++;
            mn_snapshot.online = true;
            xSemaphoreGive(mn_mutex);
        }
        xEventGroupSetBits(app_event_group, STOVE_ONLINE_BIT);

        if ((cmd_byte & 0x80) == 0) {
            /* READ request : next byte = addr */
            uint8_t addr;
            r = uart_read_bytes(STOVE_UART_NUM, &addr, 1, pdMS_TO_TICKS(MN_TIMEOUT_MS));
            if (r != 1) continue;

            uint8_t value = mn_get_ram((mn_ram_addr_t)addr);
            uint8_t response[2] = { value, (uint8_t)~value };
            mn_debug_push(MN_DIR_RX_READ,  addr, 0);
            uart_write_bytes(STOVE_UART_NUM, (const char *)response, sizeof(response));
            uart_wait_tx_done(STOVE_UART_NUM, pdMS_TO_TICKS(100));
            mn_debug_push(MN_DIR_TX_REPLY, addr, value);

            ESP_LOGD(TAG, "READ  addr=0x%02x -> value=0x%02x", addr, value);
        } else {
            /* WRITE request : next bytes = value, ~value */
            uint8_t addr = cmd_byte & 0x7F;
            uint8_t buf[2];
            r = uart_read_bytes(STOVE_UART_NUM, buf, 2, pdMS_TO_TICKS(MN_TIMEOUT_MS));
            if (r != 2) continue;

            /* Vérif checksum */
            if ((uint8_t)~buf[0] != buf[1]) {
                ESP_LOGW(TAG, "WRITE addr=0x%02x bad checksum: 0x%02x 0x%02x",
                         addr, buf[0], buf[1]);
                continue;
            }

            /* Applique + ACK */
            mn_set_ram((mn_ram_addr_t)addr, buf[0]);
            mn_debug_push(MN_DIR_RX_WRITE, addr, buf[0]);
            uart_write_bytes(STOVE_UART_NUM, (const char *)buf, sizeof(buf));
            uart_wait_tx_done(STOVE_UART_NUM, pdMS_TO_TICKS(100));
            mn_debug_push(MN_DIR_TX_REPLY, addr, buf[0]);

            ESP_LOGD(TAG, "WRITE addr=0x%02x <- 0x%02x", addr, buf[0]);
        }

        if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            mn_snapshot.tx_frames_count++;
            xSemaphoreGive(mn_mutex);
        }

        /* Rafraichit le snapshot (=pour MQTT publish) */
        update_snapshot_from_shadow();
    }
}
#endif /* legacy slave body */

/* Refresh snapshot task - keeps the MQTT-facing view in sync with the shadow.
 * Runs independently of the master poll loop so publish cadence doesn't
 * skew when the bus is slow. */
static void mn_snapshot_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        update_snapshot_from_shadow();
    }
}

/* Watchdog : détecte perte de communication avec le poêle */
static void mn_watchdog_task(void *arg)
{
    uint32_t last_rx_count = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        uint32_t cur;
        if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            cur = mn_snapshot.rx_frames_count;
            xSemaphoreGive(mn_mutex);
        } else continue;

        if (cur == last_rx_count) {
            /* Aucun byte reçu depuis 5s = poêle offline */
            if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                mn_snapshot.online = false;
                xSemaphoreGive(mn_mutex);
            }
            xEventGroupClearBits(app_event_group, STOVE_ONLINE_BIT);
        }
        last_rx_count = cur;
    }
}

esp_err_t micronova_start(void)
{
    /* Config série RWMS Micronova (=doc reverse v3 2026-07-07) :
     * UART1 @ 1200 baud, 8 data, 2 stop, no parity, no flow control.
     * PAS 38400 8N1 (=c'est la config initiale SOTA2/OTA, différent canal). */
    ESP_LOGI(TAG, "Init UART%d TX=%d RX=%d @ 1200 baud 8N2 (RWMS)",
             STOVE_UART_NUM, STOVE_UART_TX_PIN, STOVE_UART_RX_PIN);

    uart_config_t cfg = {
        .baud_rate  = 1200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_2,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* Ordre d'init exact du firmware navel v4.3 (=doc reverse V3) :
     *   uart_param_config → uart_set_line_inverse(0x24) → uart_set_pin
     *   → uart_driver_install
     * L'inversion doit être posée AVANT uart_set_pin car sur ESP-IDF v5.x,
     * uart_set_pin() peut interférer avec le bit d'inversion du GPIO matrix. */
    ESP_ERROR_CHECK(uart_param_config(STOVE_UART_NUM, &cfg));

    ESP_ERROR_CHECK(uart_set_line_inverse(STOVE_UART_NUM,
                                          UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV));
    ESP_LOGI(TAG, "UART%d line inversion set (0x24 = RXD_INV|TXD_INV)", STOVE_UART_NUM);

    ESP_ERROR_CHECK(uart_set_pin(STOVE_UART_NUM,
                                 STOVE_UART_TX_PIN,
                                 STOVE_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(uart_driver_install(STOVE_UART_NUM,
                                        MN_RX_BUFFER_SIZE,
                                        MN_TX_BUFFER_SIZE,
                                        0, NULL, 0));

    /* Re-poser l'inversion APRÈS uart_set_pin, au cas où v5.x l'aurait clearée.
     * L'appel est idempotent et fixe CONF0.RXD_INV=1 CONF0.TXD_INV=1. */
    ESP_ERROR_CHECK(uart_set_line_inverse(STOVE_UART_NUM,
                                          UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV));
    ESP_LOGI(TAG, "UART%d line inversion re-applied post pin/driver", STOVE_UART_NUM);

    /* Bus open-collector: force le pull-up interne sur RX pour maintenir l'idle
     * HIGH quand le poêle ne parle pas. uart_set_pin() désactive les pull-ups
     * par défaut donc on les réactive explicitement. */
    ESP_ERROR_CHECK(gpio_set_pull_mode((gpio_num_t)STOVE_UART_RX_PIN, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_pullup_en((gpio_num_t)STOVE_UART_RX_PIN));
    ESP_LOGI(TAG, "GPIO%d (RX) pull-up enabled", STOVE_UART_RX_PIN);

    /* GPIO22 = probable ENABLE d'un transceiver/level-shifter côté bus poêle
     * (=observé HIGH dans la capture QEMU du firmware original 2026-07-03).
     * Si ce pin n'est pas driven HIGH, le canal RX/TX peut être electriquement
     * gated coupant toute communication avec le poêle. */
    gpio_config_t gpio22_cfg = {
        .pin_bit_mask = (1ULL << 22),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio22_cfg));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)22, 1));
    ESP_LOGI(TAG, "GPIO22 driven HIGH (=probable transceiver enable)");

    mn_mutex = xSemaphoreCreateMutex();
    memset(&mn_snapshot, 0, sizeof(mn_snapshot));
    memset(mn_ram_shadow, 0, sizeof(mn_ram_shadow));

    /* Master polling loop + snapshot refresh + watchdog.
     * priority 4 = below the httpd server task, so bus activity can never
     * starve the web UI. */
    xTaskCreate(mn_slave_task,   "mn_master", 4096, NULL, 4, NULL);
    xTaskCreate(mn_snapshot_task,"mn_snap",   3072, NULL, 3, NULL);
    xTaskCreate(mn_watchdog_task,"mn_wdog",   2048, NULL, 3, NULL);
    return ESP_OK;
}


/* ============ Debug ring buffer ============
 * Circular log of the last MN_DEBUG_LOG_SIZE Micronova frames seen or emitted
 * on the bus. Exposed to the web UI via /debug/uart for a real-time console.
 */

typedef struct {
    int64_t  t_us;
    uint8_t  dir;
    uint8_t  addr;
    uint8_t  data;
} mn_dbg_entry_t;

static mn_dbg_entry_t mn_dbg_log[MN_DEBUG_LOG_SIZE];
static int            mn_dbg_head = 0;
static uint32_t       mn_dbg_seq  = 0;
static SemaphoreHandle_t mn_dbg_mutex = NULL;

static void mn_debug_lazy_init(void)
{
    if (!mn_dbg_mutex) mn_dbg_mutex = xSemaphoreCreateMutex();
}

void mn_debug_push(uint8_t dir, uint8_t addr, uint8_t data)
{
    mn_debug_lazy_init();
    if (xSemaphoreTake(mn_dbg_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    mn_dbg_log[mn_dbg_head].t_us = esp_timer_get_time();
    mn_dbg_log[mn_dbg_head].dir  = dir;
    mn_dbg_log[mn_dbg_head].addr = addr;
    mn_dbg_log[mn_dbg_head].data = data;
    mn_dbg_head = (mn_dbg_head + 1) % MN_DEBUG_LOG_SIZE;
    mn_dbg_seq++;
    xSemaphoreGive(mn_dbg_mutex);
}

uint32_t mn_debug_seq(void)
{
    return mn_dbg_seq;
}

/* Dump the whole 256-byte RAM shadow as a JSON array. Register names
 * are attached where we have symbols so the reader knows what each
 * address is without opening the source. */
char *mn_ram_dump_json(void)
{
    static const struct { uint16_t addr; const char *name; } LABELS[] = {
        /* Bank 0 (=addressable via standard read/write) */
        {MN_RAM_TH20,             "TH20"},
        {MN_RAM_STOVE_STATUS,     "STOVE_STATUS"},
        {MN_RAM_ALLARM,           "ALLARM"},
        {MN_RAM_TAMB,             "TAMB"},
        {MN_RAM_SPEGNI,           "SPEGNI"},
        {MN_RAM_ACCENDI,          "ACCENDI"},
        {MN_RAM_POT_REALE,        "POT_REALE"},
        {MN_RAM_T_FUMI,           "T_FUMI"},
        /* Bank 1 (=extended page, not yet accessible) */
        {MN_RAM_SBLOCCO,          "SBLOCCO"},
        {MN_RAM_BULBO,            "BULBO"},
        {MN_RAM_T_PUFFER_SUP,     "T_PUFFER_SUP"},
        {MN_RAM_T_CAMERA,         "T_CAMERA"},
    };

    cJSON *arr = cJSON_CreateArray();
    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (size_t i = 0; i < sizeof(LABELS)/sizeof(LABELS[0]); i++) {
            cJSON *r = cJSON_CreateObject();
            uint16_t a = LABELS[i].addr;
            char hex[8]; snprintf(hex, sizeof(hex), "0x%03X", a);
            cJSON_AddNumberToObject(r, "addr",  a);
            cJSON_AddStringToObject(r, "hex",   hex);
            cJSON_AddStringToObject(r, "name",  LABELS[i].name);
            cJSON_AddNumberToObject(r, "bank",  (a >> 8) & 0xFF);
            cJSON_AddNumberToObject(r, "value", (a < MN_RAM_MAX) ? mn_ram_shadow[a] : 0);
            cJSON_AddItemToArray(arr, r);
        }
        xSemaphoreGive(mn_mutex);
    }
    cJSON *out = cJSON_CreateObject();
    cJSON_AddItemToObject(out, "registers", arr);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return s;
}

/* Return runtime UART stats for audit: total frames RX + TX, ms since
 * last stove read, checksum error count, uptime. */
char *mn_stats_json(void)
{
    cJSON *o = cJSON_CreateObject();
    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        cJSON_AddNumberToObject(o, "rx_frames_total", mn_snapshot.rx_frames_count);
        cJSON_AddNumberToObject(o, "tx_frames_total", mn_snapshot.tx_frames_count);
        cJSON_AddNumberToObject(o, "last_activity_ms", mn_snapshot.last_updated_ms);
        cJSON_AddBoolToObject(o,   "online",          mn_snapshot.online);
        xSemaphoreGive(mn_mutex);
    }
    cJSON_AddNumberToObject(o, "now_ms",   (double)((int64_t)(esp_timer_get_time() / 1000)));
    cJSON_AddNumberToObject(o, "debug_seq", mn_debug_seq());
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

/* Live poll-list values with decoded scaling. Meant for the web UI table so
 * users can iterate on register semantics without reflashing. */
char *mn_registers_live_json(void)
{
    /* Static table mapping addr -> {name, unit, scale_hint} */
    struct { uint16_t a; const char *name; const char *unit; const char *hint; } LABELS[] = {
        { MN_RAM_STOVE_STATUS, "STOVE_STATUS",  "",       "0..9 enum" },
        { MN_RAM_TH20,         "TH20 (eau)",    "°C",     "raw" },
        { MN_RAM_ALLARM,       "ALLARM",        "code",   "0 si pas d'alarme" },
        { MN_RAM_TAMB,         "TAMB (ambiant)","°C",     "raw" },
        { MN_RAM_STATO_GESTITO,"STATO_GESTITO", "",       "état géré" },
        { MN_RAM_RESET_UTENTE, "RESET_UTENTE",  "",       "" },
        { MN_RAM_ACCENDI,      "ACCENDI",       "",       "1=allumer" },
        { MN_RAM_SPEGNI,       "SPEGNI",        "",       "1=éteindre" },
        { MN_RAM_POT_REALE,    "POT_REALE",     "%",      "0..100 pot instant" },
        { MN_RAM_T_FUMI,       "T_FUMI",        "°C",     "raw (=peut nécessiter table)" },
        /* Bank 1 (=extended, not polled by default) */
        { MN_RAM_SBLOCCO,      "SBLOCCO",       "",       "b1" },
        { MN_RAM_BULBO,        "BULBO",         "",       "b1 sonde" },
        { MN_RAM_T_PUFFER_SUP, "T_PUFFER_SUP",  "°C",     "b1 puffer haut" },
        { MN_RAM_T_CAMERA,     "T_CAMERA",      "°C",     "b1 chambre" },
    };
    const int N = (int)(sizeof(LABELS) / sizeof(LABELS[0]));

    cJSON *arr = cJSON_CreateArray();
    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < N; i++) {
            uint16_t a = LABELS[i].a;
            if (a >= MN_RAM_MAX) continue;
            cJSON *r = cJSON_CreateObject();
            char hex[8]; snprintf(hex, sizeof(hex), "0x%03X", a);
            cJSON_AddNumberToObject(r, "addr",   a);
            cJSON_AddStringToObject(r, "hex",    hex);
            cJSON_AddNumberToObject(r, "bank",   (a >> 8) & 0xFF);
            cJSON_AddStringToObject(r, "name",   LABELS[i].name);
            cJSON_AddStringToObject(r, "unit",   LABELS[i].unit);
            cJSON_AddStringToObject(r, "hint",   LABELS[i].hint);
            uint8_t v = mn_ram_shadow[a];
            cJSON_AddNumberToObject(r, "raw",    v);
            char raw_hex[6]; snprintf(raw_hex, sizeof(raw_hex), "0x%02X", v);
            cJSON_AddStringToObject(r, "raw_hex", raw_hex);
            /* is this address currently polled? */
            bool polled = false;
            for (int j = 0; j < poll_list_count; j++) if (poll_list[j] == a) { polled = true; break; }
            cJSON_AddBoolToObject(r, "polled", polled);
            cJSON_AddItemToArray(arr, r);
        }
        xSemaphoreGive(mn_mutex);
    }
    cJSON *out = cJSON_CreateObject();
    cJSON_AddItemToObject   (out, "registers", arr);
    cJSON_AddNumberToObject (out, "poll_interval_ms", poll_interval_ms);
    cJSON_AddNumberToObject (out, "poll_list_count",  poll_list_count);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return s;
}

esp_err_t mn_poll_list_set(const uint16_t *addrs, int count)
{
    if (count < 0 || count > POLL_LIST_MAX) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    for (int i = 0; i < count; i++) poll_list[i] = addrs[i];
    poll_list_count = count;
    xSemaphoreGive(mn_mutex);
    return ESP_OK;
}

char *mn_poll_list_get_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (xSemaphoreTake(mn_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < poll_list_count; i++) {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(poll_list[i]));
        }
        xSemaphoreGive(mn_mutex);
    }
    cJSON *out = cJSON_CreateObject();
    cJSON_AddItemToObject   (out, "list", arr);
    cJSON_AddNumberToObject (out, "count", poll_list_count);
    cJSON_AddNumberToObject (out, "max",   POLL_LIST_MAX);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return s;
}

esp_err_t mn_poll_interval_set(int ms)
{
    if (ms < 20 || ms > 5000) return ESP_ERR_INVALID_ARG;
    poll_interval_ms = ms;
    return ESP_OK;
}

int mn_poll_interval_get(void) { return poll_interval_ms; }


char *mn_debug_dump_json(void)
{
    mn_debug_lazy_init();
    cJSON *arr = cJSON_CreateArray();
    if (xSemaphoreTake(mn_dbg_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        /* Walk oldest -> newest */
        for (int i = 0; i < MN_DEBUG_LOG_SIZE; i++) {
            int idx = (mn_dbg_head + i) % MN_DEBUG_LOG_SIZE;
            if (mn_dbg_log[idx].t_us == 0) continue;
            cJSON *f = cJSON_CreateObject();
            cJSON_AddNumberToObject(f, "t_ms", (double)(mn_dbg_log[idx].t_us / 1000));
            cJSON_AddNumberToObject(f, "dir",  mn_dbg_log[idx].dir);
            cJSON_AddNumberToObject(f, "addr", mn_dbg_log[idx].addr);
            cJSON_AddNumberToObject(f, "data", mn_dbg_log[idx].data);
            cJSON_AddItemToArray(arr, f);
        }
        xSemaphoreGive(mn_dbg_mutex);
    }
    cJSON *out = cJSON_CreateObject();
    cJSON_AddNumberToObject(out, "seq", mn_dbg_seq);
    cJSON_AddItemToObject(out, "frames", arr);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return s;
}
