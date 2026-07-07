/**
 * openextraflame - Wi-Fi manager
 * Provides SoftAP (=provisioning) and STA (=production) modes.
 */

#pragma once

#include "esp_err.h"
#include "esp_wifi.h"

/* Start in SoftAP mode for provisioning. SSID = AP_SSID_PREFIX + MAC. */
esp_err_t wifi_bridge_start_ap(void);

/* Start in STA mode with given credentials. Auto-reconnect on disconnect.
 * Kept for source compat; internally forwards to wifi_bridge_start_dual. */
esp_err_t wifi_bridge_start_sta(const char *ssid, const char *password);

/* Start in dual APSTA mode: STA connects to configured Wi-Fi, SoftAP stays
 * up permanently as a fallback (=never brick access to the module). */
esp_err_t wifi_bridge_start_dual(const char *ssid, const char *password);

/* Trigger scan and return AP list as JSON (=for /ap.json endpoint) */
char *wifi_bridge_scan_json(void);

/* Get current status as JSON (=for /status.json endpoint) */
char *wifi_bridge_status_json(void);

/* mDNS lookup for _mqtt._tcp. Writes "IP:port" to `out` on success,
 * empty string on timeout / no service. 3 s max. Requires the module
 * to be in STA mode; will start mDNS lazily on first call. */
void wifi_bridge_mdns_query_mqtt(char *out, size_t out_size);
