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
