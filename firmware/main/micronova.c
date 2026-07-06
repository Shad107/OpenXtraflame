/**
 * openextraflame - Micronova slave listener
 *
 * ARCHITECTURE (=confirmée via QEMU + GDB) :
 * Le poêle Extraflame est MASTER Micronova. Il envoie des trames read/write
 * RAM sur le bus 38400 8N1. Nous SOMMES le module ESP32 slave, on répond.
 *
 * PROTOCOLE MICRONOVA (=à finaliser weekend après capture format réel) :
 *   Read request  : [0x00 addr]         -> [addr value ~checksum]
 *   Write request : [0x80|addr val ~val] -> [val ~val]
 *
 * Actuellement : shadow RAM interne mise à jour depuis MQTT command.
 * Quand le poêle read, on répond depuis le shadow.
 * Quand le poêle write, on met à jour le shadow puis notifie MQTT.
 */

#include <string.h>
#include "micronova.h"
#include "hardware_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "MICRONOVA";

#define MN_RX_BUFFER_SIZE   512
#define MN_TX_BUFFER_SIZE   256
#define MN_TIMEOUT_MS       1000

extern EventGroupHandle_t app_event_group;
extern const int STOVE_ONLINE_BIT;

static SemaphoreHandle_t mn_mutex;
static mn_stove_state_snapshot_t mn_snapshot;

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

/* Slave listener : lit bytes du poêle et répond
 *
 * Protocole assumé (=à valider par capture) :
 *   Byte 0 = commande : 0x00 = read RAM, 0x80|addr = write RAM
 *   Byte 1 = address (=si read) ou value (=si write)
 *   Byte 2 = complement du byte 1 (=si write, pour vérif)
 *
 * Réponse module :
 *   Read  : [value] [~value]
 *   Write : [value_confirm] [~value_confirm]
 */
static void mn_slave_task(void *arg)
{
    ESP_LOGI(TAG, "Slave listener started on UART%d (=waiting for stove)",
             STOVE_UART_NUM);

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
    ESP_LOGI(TAG, "Init UART%d TX=%d RX=%d @ %d baud 8N1",
             STOVE_UART_NUM, STOVE_UART_TX_PIN, STOVE_UART_RX_PIN, STOVE_UART_BAUD);

    uart_config_t cfg = {
        .baud_rate  = STOVE_UART_BAUD,
        .data_bits  = STOVE_UART_DATA_BITS,
        .parity     = STOVE_UART_PARITY,
        .stop_bits  = STOVE_UART_STOP_BITS,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(STOVE_UART_NUM,
                                        MN_RX_BUFFER_SIZE,
                                        MN_TX_BUFFER_SIZE,
                                        0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(STOVE_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(STOVE_UART_NUM,
                                 STOVE_UART_TX_PIN,
                                 STOVE_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    mn_mutex = xSemaphoreCreateMutex();
    memset(&mn_snapshot, 0, sizeof(mn_snapshot));
    memset(mn_ram_shadow, 0, sizeof(mn_ram_shadow));

    /* Slave listener + watchdog */
    /* priority 4 = below the httpd server task, so a noisy/floating UART RX
     * (stove disconnected) can never starve the web server */
    xTaskCreate(mn_slave_task, "mn_slave", 4096, NULL, 4, NULL);
    xTaskCreate(mn_watchdog_task, "mn_wdog", 2048, NULL, 3, NULL);
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
