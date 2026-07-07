/**
 * openextraflame - MQTT bridge to local broker
 *
 * Publishes stove state to OUT topics and subscribes to IN topics
 * for commands. Optionally publishes HA MQTT Discovery.
 */

#pragma once

#include "esp_err.h"
#include "config_nvs.h"

esp_err_t mqtt_bridge_start(const app_config_t *cfg);

/* Publish full stove snapshot as JSON to <prefix>/state */
esp_err_t mqtt_bridge_publish_state(void);

/* Publish HA MQTT discovery config for auto-integration */
esp_err_t mqtt_bridge_publish_discovery(void);

/* Quick connectivity test to a broker. Spins up a transient client with
 * the given credentials, waits up to 5 s for CONNECTED or ERROR, then
 * tears it down. Returns:
 *   ESP_OK                    = auth successful
 *   ESP_ERR_TIMEOUT           = TCP or CONNACK timeout
 *   ESP_ERR_INVALID_RESPONSE  = broker rejected credentials
 *   ESP_FAIL                  = other error
 * out_msg optional buffer (=max 96) filled with a human-readable reason.
 */
esp_err_t mqtt_bridge_test(const char *host, uint16_t port,
                           const char *user, const char *pwd, bool use_tls,
                           char *out_msg, size_t out_msg_size);
