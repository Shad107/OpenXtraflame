/**
 * openextraflame - OTA firmware update
 *
 * Uses ESP-IDF app_update API to write incoming firmware to the inactive
 * OTA partition, then swaps the boot pointer. Rollback is automatic if
 * the new firmware fails to boot within ROLLBACK_TIMEOUT_S.
 *
 * Two triggering mechanisms:
 *  - HTTP POST /ota/upload : direct binary upload from web UI
 *  - MQTT publish <prefix>/ota/pull : URL to download from
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    OTA_STATE_IDLE       = 0,
    OTA_STATE_RECEIVING  = 1,
    OTA_STATE_VERIFYING  = 2,
    OTA_STATE_APPLYING   = 3,
    OTA_STATE_REBOOTING  = 4,
    OTA_STATE_FAILED     = 99,
} ota_state_t;

typedef struct {
    ota_state_t state;
    uint32_t    total_bytes;
    uint32_t    written_bytes;
    char        message[128];
    char        active_version[64];
    char        pending_version[64];
} ota_status_t;

/* Init OTA subsystem. Marks current firmware as valid if it hasn't been yet. */
esp_err_t ota_init(void);

/* Get current status (=thread-safe snapshot) */
void ota_get_status(ota_status_t *out);

/* Streamed upload API - called from HTTP handler */
esp_err_t ota_upload_begin(size_t total_bytes);
esp_err_t ota_upload_data(const void *data, size_t len);
esp_err_t ota_upload_end(void);
esp_err_t ota_upload_abort(void);

/* Pull firmware from HTTPS URL - blocking */
esp_err_t ota_pull_from_url(const char *url);

/* Rollback to previous OTA slot */
esp_err_t ota_rollback(void);

/* Mark current firmware as valid (=disable rollback on next boot) */
esp_err_t ota_mark_valid(void);
