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

/* Registres RAM Micronova - identifiés par constants Extraflame */
typedef enum {
    /* Read (=status/temperature) */
    MN_RAM_STOVE_STATUS      = 0x21,  // placeholder, à vérifier
    MN_RAM_STATO_GESTITO     = 0x22,
    MN_RAM_MOD               = 0x23,
    MN_RAM_POT_REALE         = 0x24,
    MN_RAM_ALLARM            = 0x25,
    MN_RAM_CAUSA_STATO7      = 0x26,
    MN_RAM_SERBATORIO_VUOTO  = 0x27,
    MN_RAM_BULBO             = 0x28,
    MN_RAM_TAMB              = 0x30,
    MN_RAM_TH20              = 0x31,
    MN_RAM_T_FUMI            = 0x32,
    MN_RAM_T_CAMERA          = 0x33,
    MN_RAM_T_BOILER          = 0x34,
    MN_RAM_T_H20_RIT         = 0x35,
    MN_RAM_T_PUFFER_INF      = 0x36,
    MN_RAM_T_PUFFER_SUP      = 0x37,

    /* Write (=commands from stove) */
    MN_RAM_ACCENDI           = 0x50,
    MN_RAM_SPEGNI            = 0x51,
    MN_RAM_RESET_UTENTE      = 0x52,
    MN_RAM_SBLOCCO           = 0x53,

    MN_RAM_MAX               = 0x100,
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

/* Set RAM register value (=called from MQTT command handler)
 * Writes to internal RAM shadow. Poêle will read it via Micronova frame.
 * Ex: to change setpoint, MQTT publishes to <prefix>/cmd/setpoint 21 ->
 * mn_set_ram(MN_RAM_TAMB, 21) -> next stove read gets 21.
 */
esp_err_t mn_set_ram(mn_ram_addr_t addr, uint8_t value);

/* Get current RAM shadow value */
uint8_t mn_get_ram(mn_ram_addr_t addr);
