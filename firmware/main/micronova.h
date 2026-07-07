/**
 * openextraflame - Micronova protocol (slave listener)
 *
 * Reverse engineered via QEMU + GDB runtime capture of the original
 * Extraflame Black Label firmware (2026-07-03).
 *
 * ARCHITECTURE :
 *  Le module ESP32 est SLAVE. La carte de contrôle du POELE est MASTER.
 *  Le poêle envoie des commandes read/write RAM, le module répond.
 *
 * Physical layer :
 *  UART_NUM_1 sur ESP32
 *  TX=GPIO23, RX=GPIO5 (=Extraflame Black Label)
 *  38400 baud 8N1 no flow control
 *  Buffer réception (=DRAM 0x3ffb7761 chez Extraflame)
 *
 * Registres RAM_ Micronova (=constants identifiés dans le binaire) :
 *  Adresses HEX exactes à identifier via capture QEMU du bus (=weekend).
 *  Placeholder addresses en attendant validation.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Direction values used in the debug log */
#define MN_DIR_RX_READ   0   /* stove asked us to read a register */
#define MN_DIR_RX_WRITE  1   /* stove wrote a register into the shadow */
#define MN_DIR_TX_REPLY  2   /* we replied on the wire */

/* Debug ring buffer size (=64 frames * 12 bytes = 768 B) */
#define MN_DEBUG_LOG_SIZE 64

/* Push a frame to the debug log. Safe to call from the slave task. */
void  mn_debug_push(uint8_t dir, uint8_t addr, uint8_t data);
/* Return a heap-allocated JSON dump of the log (=caller frees). */
char *mn_debug_dump_json(void);
/* Total number of frames ever pushed since boot. */
uint32_t mn_debug_seq(void);

/* Registres RAM Micronova - VRAIES adresses extraites du binaire d'origine
 * navel (rodata offset 0x64EAE, tableaux parallèles nom/valeur).
 *
 * Encodage : octet bas = adresse dans la banque, octet haut = numéro de banque.
 * Bank 0 (0xDx) = accessible via read/write RAM standard [0x00 addr].
 * Bank 1 (0x1EA..0x1EF) = extended page, nécessite une commande spécifique
 * non encore implémentée (=T_CAMERA, BULBO, SBLOCCO, T_PUFFER_SUP).
 *
 * Registres marqués ABSENT (=0xFFFF dans le firmware d'origine) : T_BOILER,
 * T_H20_RIT, T_PUFFER_INF, MOD, STATO_GESTITO. Notre modèle ne les a pas.
 */
typedef enum {
    /* Bank 0 - lecture directe [0x00 addr] */
    MN_RAM_TH20              = 0xD0,   // eau
    MN_RAM_STOVE_STATUS      = 0xD1,
    MN_RAM_ALLARM            = 0xD2,
    MN_RAM_TAMB              = 0xD3,   // temp ambiante
    MN_RAM_RESET_UTENTE      = 0xD4,   // reset utilisateur
    MN_RAM_STATO_GESTITO     = 0xD5,   // état géré
    MN_RAM_SPEGNI            = 0xD6,   // write: éteindre
    MN_RAM_ACCENDI           = 0xD7,   // write: allumer
    MN_RAM_POT_REALE         = 0xD8,   // puissance réelle
    MN_RAM_T_FUMI            = 0xD9,   // fumées

    /* Bank 1 - non implémenté (=extended page command) */
    MN_RAM_SBLOCCO           = 0x1EA,
    MN_RAM_BULBO             = 0x1EB,
    MN_RAM_T_PUFFER_SUP      = 0x1EE,
    MN_RAM_T_CAMERA          = 0x1EF,

    /* Placeholders retirés (=absents sur notre modèle Teodora Evo I_VENT) :
     *   MOD, STATO_GESTITO, CAUSA_STATO7, SERBATORIO_VUOTO, T_BOILER,
     *   T_H20_RIT, T_PUFFER_INF, RESET_UTENTE */

    MN_RAM_MAX               = 0x200,   // bank1 max = 0x1EF+1
} mn_ram_addr_t;

/* Etats poele (=STATO_PUL_ORD_* constants d'Extraflame) */
typedef enum {
    MN_STATE_OFF                    = 0,
    MN_STATE_STARTING               = 1,
    MN_STATE_LOADING_PELLETS        = 2,
    MN_STATE_IGNITION               = 3,
    MN_STATE_WORK                   = 4,
    MN_STATE_BRAZIER_CLEANING       = 5,
    MN_STATE_FINAL_CLEANING         = 6,
    MN_STATE_STANDBY                = 7,
    MN_STATE_ALARM                  = 8,
    MN_STATE_MEMORY_ALARM           = 9,
} mn_stove_state_t;

/* Snapshot state (=publié via MQTT) */
typedef struct {
    bool             online;
    mn_stove_state_t state;
    uint8_t          power_level;
    uint8_t          set_power;
    uint8_t          alarm_code;
    float            t_ambient;
    float            t_water;
    float            t_smoke;
    float            t_chamber;
    uint16_t         set_temperature;
    uint32_t         last_updated_ms;
    uint32_t         rx_frames_count;
    uint32_t         tx_frames_count;
} mn_stove_state_snapshot_t;

/* Init UART + start slave listener task */
esp_err_t micronova_start(void);

/* Thread-safe snapshot getter */
void mn_get_snapshot(mn_stove_state_snapshot_t *out);

/* Set RAM shadow value (=used by the master poll path after a reply is read).
 * Does NOT trigger a bus write. For a user-visible write, use
 * mn_write_register() below. */
esp_err_t mn_set_ram(mn_ram_addr_t addr, uint8_t value);

/* Queue an actual Micronova WRITE frame to the stove and update the shadow.
 * Non-blocking; the master task drains the queue between polls. */
esp_err_t mn_write_register(mn_ram_addr_t addr, uint8_t value);

/* Get current RAM shadow value */
uint8_t mn_get_ram(mn_ram_addr_t addr);

/* Return the whole RAM shadow as heap-allocated JSON (=caller frees).
 * Shape: {"registers": [{"addr":48,"hex":"0x30","value":21}, ...]} */
char *mn_ram_dump_json(void);

/* Return runtime stats (=frames counts, last activity ms). */
char *mn_stats_json(void);

/* Return all polled registers with name/hex/decimal/decoded scaled value.
 * Meant for a live web UI table (no reflash needed). Caller frees. */
char *mn_registers_live_json(void);

/* Dynamic poll-list control (=no reflash). */
esp_err_t mn_poll_list_set(const uint16_t *addrs, int count);
char     *mn_poll_list_get_json(void);
esp_err_t mn_poll_interval_set(int ms);
int       mn_poll_interval_get(void);

/* Sniffer: pause the master task, capture raw UART bytes for duration_ms
 * (clamped 100..15000), fire one probe ping halfway through, return JSON.
 * Caller frees. */
char *mn_sniffer_capture(int duration_ms);

/* Send arbitrary TX bytes and capture whatever arrives on RX within
 * timeout_ms. Master task is paused for the duration. tx_hex is a
 * space-separated hex string (e.g. "20 00" or "0x00 0x21"). Caller frees. */
char *mn_raw_tx(const char *tx_hex, int timeout_ms);

/* Same but with an optional SLIP wrapper (=C0 payload C0 + escape rules). */
char *mn_raw_tx_ex(const char *tx_hex, int timeout_ms, bool wrap_slip);

/* Sweep every 8-bit address from start..end with a given opcode, capturing
 * any reply within per_addr_ms. Report which addresses responded. Caller
 * frees the returned JSON. */
char *mn_addr_scan(uint8_t opcode, uint8_t start, uint8_t end, int per_addr_ms);

/* Live UART reconfig without reboot. baud=0 means keep current.
 * rx_inv/tx_inv apply logic inversion (=for RS-232-style signaling).
 * Returns a heap-allocated status JSON. */
char *mn_reconfig_uart(int baud, int rx_inv, int tx_inv);

/* Extended reconfig also lets you switch stop bits (=1 or 2, 0=keep). */
char *mn_reconfig_uart_ex(int baud, int rx_inv, int tx_inv, int stop_bits);

/* Sample GPIO5 (RX pin) raw at ~100kHz for duration_ms.
 * Returns transitions/duty ratio to determine if poêle is driving any signal
 * at all - bypasses the UART peripheral entirely. */
char *mn_rx_raw_sample(int duration_ms);

/* Sample GPIO23 (TX pin) as INPUT while firing a burst of UART TX bytes.
 * Verifies our TX line actually pulses when the UART peripheral drives it. */
char *mn_tx_self_check(void);
