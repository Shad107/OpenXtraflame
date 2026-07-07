/**
 * openextraflame - NVS-backed configuration
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Stove model - matches Extraflame STOVE_TYPE_I_* constants */
typedef enum {
    STOVE_TYPE_UNKNOWN     = 0,
    STOVE_TYPE_I_CALD      = 1,   // caldaia
    STOVE_TYPE_I_CANAL     = 2,   // canalizzato
    STOVE_TYPE_I_CANAL_2   = 3,
    STOVE_TYPE_I_CANAL_3   = 4,
    STOVE_TYPE_I_CANAL_4   = 5,
    STOVE_TYPE_I_IDRO      = 6,   // hydro
    STOVE_TYPE_I_IDRO_2    = 7,
    STOVE_TYPE_I_VENT      = 8,   // ventilato (=Teodora Evo)
    STOVE_TYPE_I_VENT_2    = 9,
    STOVE_TYPE_I_VENT_3    = 10,
    STOVE_TYPE_I_VENT_4    = 11,
    STOVE_TYPE_I_VENT_5    = 12,
} stove_type_t;

typedef struct {
    bool     provisioned;

    /* Wi-Fi STA credentials */
    char     wifi_ssid[64];        // 802.11 SSID max is 32, 64 is plenty
    char     wifi_password[128];   // WPA2 PSK can be 63-char passphrase or 64 hex, allow overhead

    /* MQTT broker config */
    char     mqtt_host[128];        // e.g. 192.168.1.10
    uint16_t mqtt_port;             // e.g. 1883
    char     mqtt_username[128];    // some brokers use long JWT-style tokens
    char     mqtt_password[256];    // HA-generated broker passwords can hit 64+, keep headroom
    char     mqtt_topic_prefix[64]; // e.g. "extraflame"
    bool     mqtt_use_tls;

    /* Stove identification */
    stove_type_t stove_type;
    char     stove_name[32];        // friendly name for HA

    /* Advanced */
    bool     ha_discovery_enabled;  // publish MQTT discovery topics
    uint16_t publish_interval_ms;   // between OUT/status publishes

#ifdef TARGET_BLACKLABEL
    /* Guardian Mode - OPT-IN, disabled by default
     * ONLY available on TARGET_BLACKLABEL because it requires the
     * secure_code and stove_model credentials that Extraflame cloud
     * recognizes for this specific module identity.
     *
     * Not compiled on TARGET_EXTERNAL since spare ESP32s have no
     * cloud identity and would be rejected by Omnyvore backend.
     *
     * Connects module to Extraflame cloud in read-only mode to capture
     * OTA firmwares and push them to a user-owned archive server.
     * See docs/GUARDIAN_MODE_IDEA.md for details and legal considerations.
     */
    bool     guardian_enabled;             // default false
    char     guardian_secure_code[16];     // required if enabled (=from original dump)
    char     guardian_stove_model[16];     // required if enabled (=from original dump)
    char     guardian_archive_url[256];    // POST captured firmware here
    char     guardian_archive_token[128];  // optional bearer token
    uint8_t  guardian_action_mode;         // 0=archive_only, 1=archive_notify, 2=archive_apply
#endif
} app_config_t;

/* Load config from NVS - fills defaults if not present */
esp_err_t config_nvs_load(app_config_t *cfg);

/* Save config to NVS */
esp_err_t config_nvs_save(const app_config_t *cfg);

/* Reset config (=factory defaults) */
esp_err_t config_nvs_reset(void);

/* Set default values in cfg struct */
void config_nvs_defaults(app_config_t *cfg);

#ifdef TARGET_BLACKLABEL
/* Read the original Extraflame stove identity from the preserved `secret1`
 * NVS partition (only present after a TARGET_BLACKLABEL reflash that keeps
 * the original partition intact). Any out pointer may be NULL; buffers are
 * always NUL-terminated (empty string if the key is missing/blank).
 *
 * Source (reverse engineered from the original dump):
 *   secure_code -> partition "secret1", namespace "product",  key "secure_code"
 *   stove_model -> partition "secret1", namespace "product",  key "stove_model"
 *   matricola   -> partition "secret1", namespace "collaudo", key "matricola"
 *
 * Returns ESP_OK if the partition mounted (even if some keys are absent),
 * or an error if the partition is missing (e.g. on TARGET_EXTERNAL). */
esp_err_t config_nvs_read_stove_secrets(char *secure_code, size_t secure_code_len,
                                        char *stove_model, size_t stove_model_len,
                                        char *matricola,   size_t matricola_len);
#endif
