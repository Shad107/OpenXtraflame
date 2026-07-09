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

/* Registres RAM Micronova STANDARD - documenté par la communauté
 * (=philibertc/micronova_controller, ridiculouslab).
 *
 * Ces adresses sont validées en LIVE contre notre Teodora Evo I_VENT
 * (2026-07-07) : elles donnent des valeurs cohérentes avec l'état du poêle
 * (0x01=61→30.5°C ambient ; 0x21=0→OFF ; 0x3E=0→pas de fumées) alors que
 * les adresses 0xD0-0xEF du reverse navel donnent des valeurs bidon (0x20).
 *
 * La doc du reverse navel (offset rodata 0x64EAE) pointe probablement vers
 * un espace mémoire INTERNE du module Extraflame Black Label, pas vers les
 * registres du contrôleur poêle.
 */
typedef enum {
    /* Températures Micronova standard - encodage temp×2 pour °C */
    MN_RAM_TAMB              = 0x01,   // ambient
    MN_RAM_TH20              = 0x03,   // eau (=absent sur I_VENT, ignoré)
    MN_RAM_STOVE_STATE       = 0x21,   // 0=OFF 1=Starting 2=PelletLoading 3=Ignition 4=Work 5=Cleaning 6=FinalCleaning 7=Standby 8=PelletAlarm 9=IgnitionFailAlarm
    MN_RAM_WATER_PRESSURE    = 0x3C,
    MN_RAM_FLAME_POWER_STD   = 0x34,   // fallback philibertc, souvent 0 sur Extraflame
    MN_RAM_FUMES_TEMP_STD    = 0x3E,   // fallback philibertc, souvent 0 sur Extraflame

    /* Adresses spécifiques Extraflame Teodora Evo I_VENT - découvertes empiriques
     * 2026-07-07 en live: la convention philibertc 0x7F/0x9F donne 0, les vraies
     * adresses sur Teodora sont dans la plage 0x40-0x77.
     */
    MN_RAM_POWER_SET         = 0x4F,   // P.set puissance affichée (=fluctue, mirror du EEPROM)
    MN_RAM_POWER_REAL        = 0x34,   // POT_REALE = P.real (=firmware reverse table RAM @ 0x64f74)
    MN_RAM_TEMP_SET          = 0x54,   // consigne ambient RAM (=×2)
    /* EEPROM SET_* pour I_VENT (=Teodora Evo). Bank 1 → prefix 0x100.
     * Adresses extraites de la Addrs_dyn table 42-47 (=I_VENT family).
     * Validé 2026-07-08: EEPROM 0x7F=P.set (=4 quand écran affiche 4),
     * EEPROM 0x7D=consigne (=27 quand écran affiche 27). */
    MN_EEP_POWER_SET_IVENT   = 0x17F,  // EEPROM_SET_POWER_ADDR pour I_VENT
    MN_EEP_TEMP_SET_IVENT    = 0x17D,  // EEPROM_SET_AMB_ADDR pour I_VENT
    /* Registres RAM I_VENT dérivés via Addrs_dyn @ 0x6491c (=table complète) */
    MN_RAM_SERBATORIO_VUOTO  = 0x0DF,  // Trémie vide (=1 quand plus de pellets)
    MN_RAM_CAUSA_STATO7      = 0x0E0,  // Cause de l'état 7 (=raison arrêt/blocage)
    MN_RAM_MODULATION        = 0x0E2,  // Modulation actuelle
    /* Compteurs maintenance (=firmware reverse, table I_VENT, 16-bit LSB+MSB).
     * Heures par niveau P1..P5 + total + nombre de démarrages. */
    MN_EEP_CTR_H_P1_LSB      = 0x1D0,  // COUNTERS_H_1_LSB
    MN_EEP_CTR_H_P1_MSB      = 0x1D1,
    MN_EEP_CTR_H_P2_LSB      = 0x1D2,
    MN_EEP_CTR_H_P2_MSB      = 0x1D3,
    MN_EEP_CTR_H_P3_LSB      = 0x1D4,
    MN_EEP_CTR_H_P3_MSB      = 0x1D5,
    MN_EEP_CTR_H_P4_LSB      = 0x1D6,
    MN_EEP_CTR_H_P4_MSB      = 0x1D7,
    MN_EEP_CTR_H_P5_LSB      = 0x1D8,
    MN_EEP_CTR_H_P5_MSB      = 0x1D9,
    MN_EEP_CTR_H_TOT_LSB     = 0x1EA,  // COUNTERS_H_TOT (=heures totales)
    MN_EEP_CTR_H_TOT_MSB     = 0x1EB,
    MN_EEP_CTR_STARTS_LSB    = 0x1EE,  // COUNTERS_STARTS (=nombre démarrages)
    MN_EEP_CTR_STARTS_MSB    = 0x1EF,
    /* Chrono I_VENT - master enable + 4 programmes hebdo (=firmware reverse) */
    MN_EEP_CHRONO_ENABLE     = 0x1AE,  // master on/off (=global chrono)
    MN_EEP_CHRONO_EN1        = 0x1AF,
    MN_EEP_CHRONO_EN2        = 0x1B0,
    MN_EEP_CHRONO_EN3        = 0x1B1,
    MN_EEP_CHRONO_EN4        = 0x1B2,
    /* CHRONO1 : START, STOP, DAY1..7, TEMP */
    MN_EEP_CHRONO1_START     = 0x14D,
    MN_EEP_CHRONO1_STOP      = 0x14E,
    MN_EEP_CHRONO1_DAY1      = 0x14F,  /* lundi */
    MN_EEP_CHRONO1_DAY7      = 0x155,  /* dimanche (=DAY1+6) */
    MN_EEP_CHRONO1_TEMP      = 0x156,
    /* CHRONO2 : START at DAY7+1 = 0x157, and so on ; contigu */
    MN_EEP_CHRONO2_START     = 0x157,
    MN_EEP_CHRONO2_TEMP      = 0x160,
    MN_EEP_CHRONO3_START     = 0x161,
    MN_EEP_CHRONO3_TEMP      = 0x16A,
    MN_EEP_CHRONO4_START     = 0x16B,
    MN_EEP_CHRONO4_TEMP      = 0x174,
    MN_RAM_FUMES_TEMP        = 0x5A,   // temp fumées °C raw (validé 2026-07-07 20:24: 44 quand écran affiche 44)
    MN_RAM_FLAME_POWER       = 0x42,   // puissance flamme instantanée (empirique)

    /* Alias pour compat legacy */
    MN_RAM_TEMP_GET          = MN_RAM_TEMP_SET,
    MN_RAM_POWER_GET         = MN_RAM_POWER_SET,

    /* Alias legacy pour compat (=à supprimer plus tard) */
    MN_RAM_STOVE_STATUS      = MN_RAM_STOVE_STATE,
    MN_RAM_T_FUMI            = MN_RAM_FUMES_TEMP,
    MN_RAM_POT_REALE         = MN_RAM_POWER_GET,   /* now empirically = pwr%2 or step */
    MN_RAM_ALLARM            = 0x40,               /* WARN: guess, non validé */
    MN_RAM_ACCENDI           = MN_RAM_STOVE_STATE, /* ON = write 0x21 = 1 */
    MN_RAM_SPEGNI            = MN_RAM_STOVE_STATE, /* OFF = write 0x21 = 6 */
    MN_RAM_SBLOCCO           = 0x41,               /* WARN: guess */
    MN_RAM_T_CAMERA          = MN_RAM_FUMES_TEMP,  /* alias approx */

    MN_RAM_MAX               = 0x200,
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

/* Type de poêle (=enum stove_type_t défini dans config_nvs.h,
 * partagé avec firmware reverse Black Label v1.8). */
#include "config_nvs.h"

const char *mn_stove_type_name(stove_type_t t);
stove_type_t mn_detected_stove_type(void);
const char *mn_get_stove_model(void);       /* stove_model brut depuis secret1 (=fabrication) */
const char *mn_get_stove_matricola(void);   /* matricola brut depuis secret1 */
const char *mn_get_stove_secure_code(void); /* secure_code (=password cloud MQTT) */

/* Snapshot state (=publié via MQTT) */
typedef struct {
    bool             online;
    stove_type_t     stove_type;      /* Modèle détecté (=Addrs_dyn selection) */
    mn_stove_state_t state;
    uint8_t          power_level;    /* P.set displayed */
    uint8_t          power_real;     /* P.real = POT_REALE (0x34) */
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
    /* Compteurs maintenance (=16-bit reconstruits depuis LSB+MSB EEPROM) */
    uint16_t         hours_total;
    uint16_t         starts_total;
    uint16_t         hours_p1;
    uint16_t         hours_p2;
    uint16_t         hours_p3;
    uint16_t         hours_p4;
    uint16_t         hours_p5;
    /* Pellets kg calculés depuis compteurs + config */
    float            pellets_total_kg;      /* Σ hours_pn × conso_n */
    float            pellets_since_refill_kg;
    float            pellets_remaining_kg;
    float            pellets_cost_lifetime_eur;
    float            pellets_days_left;     /* estimation avg 7d */
    uint32_t         pellets_empty_ts;      /* unix ts fin trémie estimée */
    float            pellets_kg_per_day;    /* conso moyenne 7j */
    /* Alarmes décomposées bit par bit (=depuis RAM_ALLARM byte).
     * Ordre issu du firmware original (=strings ALLARM_*_BIT ordonnées). */
    bool             alarm_sonda_fumi;      /* bit 0 - sonde fumées défectueuse */
    bool             alarm_hot_fumi;        /* bit 1 - T° fumées trop élevée */
    bool             alarm_fumi_corto;      /* bit 2 - sonde fumées court-circuit */
    bool             alarm_aspiratore;      /* bit 3 - aspirateur défectueux */
    bool             alarm_no_accensione;   /* bit 4 - échec allumage */
    bool             alarm_no_fiamma;       /* bit 5 - perte de flamme */
    bool             alarm_depression;      /* bit 6 - dépression insuffisante */
    bool             alarm_coclea_cmd;      /* bit 7 - alarme commande vis sans fin */
    /* Maintenance : compteurs pour prochaine intervention */
    uint16_t         hours_since_service;   /* h_total - snapshot_at_reset */
    uint16_t         starts_since_cleaning; /* starts - snapshot_at_reset */
    int16_t          hours_before_service;  /* seuil - hours_since_service (peut <0) */
    int16_t          starts_before_cleaning;/* seuil - starts_since_cleaning */
    /* État nettoyage automatique brasero (=STATO_PUL_ORD_*, N/A pour I_VENT) */
    uint8_t          cleaning_state;        /* 0=off, 1=apertura, 2=aperto, 3=chiusura */
    /* Trémie et cause état 7 (=I_VENT Addrs_dyn) */
    bool             tremie_vide;           /* SERBATORIO_VUOTO : true si plus de pellets */
    uint8_t          causa_stato7;          /* Raison arrêt/blocage si state=7 */
    uint8_t          modulation;            /* Modulation actuelle (=0-100) */
} mn_stove_state_snapshot_t;

/* Init UART + start slave listener task */
esp_err_t micronova_start(void);

/* Fournir la référence à la config app (=lue pour calcul pellet kg).
 * Appelé une fois au boot après config_nvs_load(). */
void mn_set_config_ref(const void *cfg);

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

/* Lit les 4 programmes chrono depuis le shadow (=si polled). Renvoie JSON
 * détaillé avec pour chaque programme: enabled, start_hhmm, stop_hhmm,
 * days[7] (=lundi-dimanche), temp_c. Caller frees. */
char *mn_chrono_json(void);

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
