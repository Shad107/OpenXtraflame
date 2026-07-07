/**
 * openextraflame - in-memory ring buffer for ESP_LOGx output
 *
 * Hooks esp_log_set_vprintf to duplicate every log line into a
 * circular text buffer that /debug/logs serves as JSON. Lets users
 * see the same stream as miniterm/screen without the CH340G.
 */
#pragma once

#include "esp_err.h"

/* Initialise the ring buffer and install the log hook. Safe to call
 * once at boot, additional calls are no-ops. */
esp_err_t log_ring_init(void);

/* Dump the entire buffer contents to caller-owned heap. Caller frees.
 * Returns a null-terminated string; NULL on OOM. */
char *log_ring_dump(void);
