/**
 * openextraflame - MQTT bridge implementation
 *
 * Publishes stove state at cfg->publish_interval_ms.
 * Subscribes to <prefix>/cmd/# for control:
 *   <prefix>/cmd/on        -> mn_turn_on
 *   <prefix>/cmd/off       -> mn_turn_off
 *   <prefix>/cmd/setpoint  -> mn_set_temperature
 *   <prefix>/cmd/power     -> mn_set_power
 *   <prefix>/cmd/reset_alarm -> mn_clear_alarm
 */

#include <string.h>
#include <stdlib.h>
#include "mqtt_bridge.h"
#include "micronova.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

extern EventGroupHandle_t app_event_group;
extern const int MQTT_CONNECTED_BIT;

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t client = NULL;
static app_config_t local_cfg;
static char sub_topic_prefix[128] = "";

static void handle_command(const char *topic, size_t tlen,
                            const char *data,  size_t dlen)
{
    /* Slave design : nous ne pouvons pas envoyer d'ordre directement au poêle.
     * Au lieu de ça, on écrit dans le shadow RAM. Le poêle lira cette valeur
     * quand il polle le registre correspondant, appliquant ainsi la commande.
     */
    if (strstr(topic, "/cmd/on"))          { mn_set_ram(MN_RAM_ACCENDI, 1);      return; }
    if (strstr(topic, "/cmd/off"))         { mn_set_ram(MN_RAM_SPEGNI, 1);       return; }
    if (strstr(topic, "/cmd/reset_alarm")) { mn_set_ram(MN_RAM_SBLOCCO, 1);      return; }
    if (strstr(topic, "/cmd/setpoint")) {
        char buf[8] = {0};
        if (dlen < sizeof(buf)) {
            memcpy(buf, data, dlen);
            int v = atoi(buf);
            mn_set_ram(MN_RAM_TAMB, (uint8_t)v);
        }
        return;
    }
    if (strstr(topic, "/cmd/power")) {
        char buf[4] = {0};
        if (dlen < sizeof(buf)) {
            memcpy(buf, data, dlen);
            int v = atoi(buf);
            mn_set_ram(MN_RAM_POT_REALE, (uint8_t)v);
        }
        return;
    }
    ESP_LOGW(TAG, "Unhandled command topic (len %d)", (int)tlen);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "Connected to broker");
        xEventGroupSetBits(app_event_group, MQTT_CONNECTED_BIT);
        char topic[160];
        snprintf(topic, sizeof(topic), "%s/cmd/#", sub_topic_prefix);
        esp_mqtt_client_subscribe(client, topic, 1);
        if (local_cfg.ha_discovery_enabled) {
            mqtt_bridge_publish_discovery();
        }
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected");
        xEventGroupClearBits(app_event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_DATA:
        handle_command(event->topic, event->topic_len,
                        event->data,  event->data_len);
        break;
    default:
        break;
    }
}

static void publish_task(void *arg)
{
    for (;;) {
        if ((xEventGroupGetBits(app_event_group) & MQTT_CONNECTED_BIT) != 0) {
            mqtt_bridge_publish_state();
        }
        vTaskDelay(pdMS_TO_TICKS(local_cfg.publish_interval_ms));
    }
}

esp_err_t mqtt_bridge_start(const app_config_t *cfg)
{
    memcpy(&local_cfg, cfg, sizeof(local_cfg));
    snprintf(sub_topic_prefix, sizeof(sub_topic_prefix),
             "%s/%s", cfg->mqtt_topic_prefix, cfg->stove_name);

    char uri[192];
    snprintf(uri, sizeof(uri), "%s://%s:%u",
             cfg->mqtt_use_tls ? "mqtts" : "mqtt",
             cfg->mqtt_host, (unsigned)cfg->mqtt_port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri       = uri,
        .credentials.username     = (cfg->mqtt_username[0] ? cfg->mqtt_username : NULL),
        .credentials.authentication.password = (cfg->mqtt_password[0] ? cfg->mqtt_password : NULL),
        .session.keepalive        = 30,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    xTaskCreate(publish_task, "mqtt_pub", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "MQTT bridge started, URI=%s prefix=%s", uri, sub_topic_prefix);
    return ESP_OK;
}

esp_err_t mqtt_bridge_publish_state(void)
{
    mn_stove_state_snapshot_t s;
    mn_get_snapshot(&s);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o,   "online",    s.online);
    cJSON_AddNumberToObject(o, "state",     s.state);
    cJSON_AddNumberToObject(o, "power",     s.power_level);
    cJSON_AddNumberToObject(o, "alarm",     s.alarm_code);
    cJSON_AddNumberToObject(o, "t_ambient", s.t_ambient);
    cJSON_AddNumberToObject(o, "t_water",   s.t_water);
    cJSON_AddNumberToObject(o, "t_smoke",   s.t_smoke);
    cJSON_AddNumberToObject(o, "setpoint",  s.set_temperature);
    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);

    char topic[160];
    snprintf(topic, sizeof(topic), "%s/state", sub_topic_prefix);
    esp_mqtt_client_publish(client, topic, json, 0, 0, 0);
    free(json);
    return ESP_OK;
}

esp_err_t mqtt_bridge_publish_discovery(void)
{
    /* Publish HA MQTT Discovery for a climate entity + several sensors */
    /* Skeleton: user extends per stove type */
    ESP_LOGI(TAG, "TODO: publish HA discovery topics");
    return ESP_OK;
}
