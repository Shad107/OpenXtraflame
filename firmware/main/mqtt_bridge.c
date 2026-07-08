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
#include <stdio.h>
#include "mqtt_bridge.h"
#include "micronova.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
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
    /* Master design : the ESP32 polls the stove and pushes writes on the bus.
     * mn_write_register() updates the local shadow AND queues a Micronova
     * write frame that the master task will send at the next slot. */
    /* Standard Micronova: STOVE_STATE register (=0x21) drives all commands. */
    if (strstr(topic, "/cmd/on"))          { mn_write_register(MN_RAM_STOVE_STATE, 0x01); return; }
    if (strstr(topic, "/cmd/off"))         { mn_write_register(MN_RAM_STOVE_STATE, 0x06); return; }
    if (strstr(topic, "/cmd/reset_alarm")) { mn_write_register(MN_RAM_STOVE_STATE, 0x00); return; }
    if (strstr(topic, "/cmd/setpoint")) {
        char buf[8] = {0};
        if (dlen < sizeof(buf)) {
            memcpy(buf, data, dlen);
            int v = atoi(buf);
            /* Micronova standard: TEMP_SET register at 0x7D, encoding raw °C */
            mn_write_register(MN_RAM_TEMP_SET, (uint8_t)v);
        }
        return;
    }
    if (strstr(topic, "/cmd/power")) {
        char buf[4] = {0};
        if (dlen < sizeof(buf)) {
            memcpy(buf, data, dlen);
            int v = atoi(buf);
            mn_write_register(MN_RAM_POWER_SET, (uint8_t)v);
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

    /* Last Will Testament: on disconnect the broker publishes 'offline' to
     * the availability topic, HA immediately greys out all our entities
     * without waiting for a keepalive timeout. */
    static char lwt_topic[160];
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/availability", sub_topic_prefix);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri       = uri,
        .credentials.username     = (cfg->mqtt_username[0] ? cfg->mqtt_username : NULL),
        .credentials.authentication.password = (cfg->mqtt_password[0] ? cfg->mqtt_password : NULL),
        .session.keepalive        = 30,
        .session.last_will.topic  = lwt_topic,
        .session.last_will.msg    = "offline",
        .session.last_will.msg_len = 7,
        .session.last_will.qos    = 1,
        .session.last_will.retain = 1,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    xTaskCreate(publish_task, "mqtt_pub", 4096, NULL, 4, NULL);
    /* Diagnostic: prints the exact username the client will send to the
     * broker (=so users can catch cases where NVS is silently empty even
     * though the UI form had something in it) and the length of the
     * stored password (=NOT its value). No secret in the log. */
    ESP_LOGI(TAG, "MQTT bridge started, URI=%s prefix=%s user='%s' pwd_len=%d",
             uri, sub_topic_prefix,
             cfg->mqtt_username[0] ? cfg->mqtt_username : "(=empty, sending NULL)",
             (int)strlen(cfg->mqtt_password));
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
    cJSON_AddNumberToObject(o, "power_real", s.power_real);
    cJSON_AddNumberToObject(o, "alarm",     s.alarm_code);
    cJSON_AddNumberToObject(o, "t_ambient", s.t_ambient);
    /* t_water only if stove is a hydro model (=I_CALD/I_IDRO). On the
     * ventilated I_VENT models the register 0x03 is absent and returns 0,
     * so publishing it produces a confusing constant 0 in HA. */
    if (local_cfg.stove_type == STOVE_TYPE_I_CALD ||
        local_cfg.stove_type == STOVE_TYPE_I_IDRO ||
        local_cfg.stove_type == STOVE_TYPE_I_IDRO_2) {
        cJSON_AddNumberToObject(o, "t_water", s.t_water);
    }
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

/* ---- Transient test client ---- */

typedef struct {
    SemaphoreHandle_t done;
    esp_err_t         result;
    char              msg[96];
} test_ctx_t;

static void test_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    test_ctx_t *ctx = (test_ctx_t *)arg;
    esp_mqtt_event_handle_t event = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ctx->result = ESP_OK;
        snprintf(ctx->msg, sizeof(ctx->msg), "Connexion + auth OK");
        xSemaphoreGive(ctx->done);
        break;
    case MQTT_EVENT_ERROR:
        if (event->error_handle) {
            if (event->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_BAD_USERNAME) {
                ctx->result = ESP_ERR_INVALID_RESPONSE;
                snprintf(ctx->msg, sizeof(ctx->msg), "Broker : credentials refusés");
            } else if (event->error_handle->connect_return_code != 0) {
                ctx->result = ESP_ERR_INVALID_RESPONSE;
                snprintf(ctx->msg, sizeof(ctx->msg), "Broker : refuse (CONNACK=%d)",
                         event->error_handle->connect_return_code);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ctx->result = ESP_ERR_TIMEOUT;
                snprintf(ctx->msg, sizeof(ctx->msg), "TCP/TLS impossible (=port fermé, TLS mismatch, etc.)");
            } else {
                ctx->result = ESP_FAIL;
                snprintf(ctx->msg, sizeof(ctx->msg), "Erreur MQTT interne");
            }
        } else {
            ctx->result = ESP_FAIL;
            snprintf(ctx->msg, sizeof(ctx->msg), "Erreur inconnue");
        }
        xSemaphoreGive(ctx->done);
        break;
    default:
        break;
    }
}

esp_err_t mqtt_bridge_test(const char *host, uint16_t port,
                           const char *user, const char *pwd, bool use_tls,
                           char *out_msg, size_t out_msg_size)
{
    test_ctx_t ctx = {0};
    ctx.done = xSemaphoreCreateBinary();
    ctx.result = ESP_ERR_TIMEOUT;
    snprintf(ctx.msg, sizeof(ctx.msg), "Pas de réponse du broker");

    char uri[192];
    snprintf(uri, sizeof(uri), "%s://%s:%u",
             use_tls ? "mqtts" : "mqtt", host, (unsigned)port);

    /* Distinct client_id so the transient test client doesn't clash with
     * the main long-lived client on the broker (=broker would kick the
     * older session on identical client_id, which flapped the real
     * client during the test). MAC + suffix guarantees uniqueness. */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char client_id[48];
    snprintf(client_id, sizeof(client_id), "openxtraflame_test_%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .credentials.username = (user && user[0]) ? user : NULL,
        .credentials.authentication.password = (pwd && pwd[0]) ? pwd : NULL,
        .credentials.client_id = client_id,
        .session.keepalive = 5,
        .network.timeout_ms = 3000,
        .network.disable_auto_reconnect = true,
    };
    esp_mqtt_client_handle_t c = esp_mqtt_client_init(&cfg);
    if (!c) {
        vSemaphoreDelete(ctx.done);
        if (out_msg) snprintf(out_msg, out_msg_size, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(c, ESP_EVENT_ANY_ID, test_event_handler, &ctx);
    esp_mqtt_client_start(c);
    xSemaphoreTake(ctx.done, pdMS_TO_TICKS(5000));
    esp_mqtt_client_stop(c);
    esp_mqtt_client_destroy(c);
    vSemaphoreDelete(ctx.done);

    if (out_msg) strncpy(out_msg, ctx.msg, out_msg_size - 1);
    return ctx.result;
}

/* ---- HA MQTT Discovery ---- */

static char device_id[24] = "";       /* openxtraflame_XXXXXX */
static char state_topic[160] = "";    /* <prefix>/<stove>/state */
static char avail_topic[160] = "";    /* <prefix>/<stove>/availability */

/* Publish a single discovery config topic. `component` = sensor / switch / etc. */
static void publish_disco(const char *component, const char *obj_id, cJSON *payload)
{
    char topic[192];
    snprintf(topic, sizeof(topic),
             "homeassistant/%s/%s/%s/config",
             component, device_id, obj_id);
    /* Common bits every entity needs */
    cJSON_AddStringToObject(payload, "unique_id",       obj_id);
    cJSON_AddStringToObject(payload, "object_id",       obj_id);
    cJSON_AddStringToObject(payload, "availability_topic", avail_topic);
    cJSON_AddStringToObject(payload, "payload_available",     "online");
    cJSON_AddStringToObject(payload, "payload_not_available", "offline");

    /* Device block shared by all entities so HA groups them */
    cJSON *dev = cJSON_CreateObject();
    cJSON *ids = cJSON_CreateArray();
    cJSON_AddItemToArray(ids, cJSON_CreateString(device_id));
    cJSON_AddItemToObject(dev, "identifiers",  ids);
    cJSON_AddStringToObject(dev, "manufacturer", "Extraflame");
    cJSON_AddStringToObject(dev, "model",        "OpenXtraflame Black Label");
    cJSON_AddStringToObject(dev, "name",         local_cfg.stove_name);
    const esp_app_desc_t *desc = esp_app_get_description();
    cJSON_AddStringToObject(dev, "sw_version", desc ? desc->version : "unknown");
    cJSON_AddItemToObject(payload, "device", dev);

    char *json = cJSON_PrintUnformatted(payload);
    esp_mqtt_client_publish(client, topic, json, 0, 1 /*qos*/, 1 /*retain*/);
    free(json);
    cJSON_Delete(payload);
}

/* Helpers to build the various entity types with minimal repetition. */
static cJSON *sensor(const char *name, const char *json_key,
                     const char *unit, const char *device_class)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "name",  name);
    cJSON_AddStringToObject(p, "state_topic", state_topic);
    char tpl[64]; snprintf(tpl, sizeof(tpl), "{{ value_json.%s }}", json_key);
    cJSON_AddStringToObject(p, "value_template", tpl);
    if (unit)         cJSON_AddStringToObject(p, "unit_of_measurement", unit);
    if (device_class) cJSON_AddStringToObject(p, "device_class",        device_class);
    return p;
}

esp_err_t mqtt_bridge_publish_discovery(void)
{
    if (!client) return ESP_ERR_INVALID_STATE;

    /* Compute stable device_id from Wi-Fi MAC once. */
    if (device_id[0] == '\0') {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(device_id, sizeof(device_id),
                 "openxtraflame_%02X%02X%02X",
                 mac[3], mac[4], mac[5]);
    }
    /* Topics already built during mqtt_bridge_start */
    snprintf(state_topic, sizeof(state_topic), "%s/state",        sub_topic_prefix);
    snprintf(avail_topic, sizeof(avail_topic), "%s/availability", sub_topic_prefix);

    ESP_LOGI(TAG, "Publishing HA discovery for device_id=%s", device_id);

    /* --- Sensors --- */
    publish_disco("sensor", "t_ambient",
                  sensor("Température ambiante", "t_ambient", "°C", "temperature"));
    publish_disco("sensor", "t_smoke",
                  sensor("Température fumées",   "t_smoke",   "°C", "temperature"));
    if (local_cfg.stove_type == STOVE_TYPE_I_CALD ||
        local_cfg.stove_type == STOVE_TYPE_I_IDRO ||
        local_cfg.stove_type == STOVE_TYPE_I_IDRO_2) {
        publish_disco("sensor", "t_water",
                      sensor("Température eau",  "t_water",   "°C", "temperature"));
    }
    publish_disco("sensor", "power_level",
                  sensor("Puissance",            "power",     NULL, NULL));
    publish_disco("sensor", "power_real",
                  sensor("Puissance réelle",     "power_real", NULL, NULL));
    publish_disco("sensor", "alarm_code",
                  sensor("Code alarme",          "alarm",     NULL, NULL));

    /* Stove state as text sensor with a mapping in value_template */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "État poêle");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{% set s = value_json.state %}"
            "{{ {0:'Off',1:'Allumage',2:'Chargement pellets',3:'Ignition',"
            "4:'En marche',5:'Nettoyage brasier',6:'Nettoyage final',"
            "7:'Standby',8:'Alarme',9:'Alarme mémoire'}.get(s, 'Inconnu') }}");
        publish_disco("sensor", "state", p);
    }

    /* --- Binary sensor Online --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Online");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{{ 'ON' if value_json.online else 'OFF' }}");
        cJSON_AddStringToObject(p, "payload_on",  "ON");
        cJSON_AddStringToObject(p, "payload_off", "OFF");
        cJSON_AddStringToObject(p, "device_class", "connectivity");
        publish_disco("binary_sensor", "online", p);
    }

    /* --- Switch on/off --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Poêle");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{{ 'ON' if value_json.state in [1,2,3,4,5,7] else 'OFF' }}");
        char cmd_on[160], cmd_off[160];
        snprintf(cmd_on,  sizeof(cmd_on),  "%s/cmd/on",  sub_topic_prefix);
        snprintf(cmd_off, sizeof(cmd_off), "%s/cmd/off", sub_topic_prefix);
        cJSON *ct = cJSON_CreateObject();
        cJSON_AddStringToObject(ct, "on",  cmd_on);
        cJSON_AddStringToObject(ct, "off", cmd_off);
        /* HA switch supports command_topic on/off directly; use two entities */
        cJSON_AddStringToObject(p, "command_topic", cmd_on);
        cJSON_AddStringToObject(p, "payload_on",  "1");
        cJSON_AddStringToObject(p, "payload_off", "0");
        cJSON_AddStringToObject(p, "state_on",  "ON");
        cJSON_AddStringToObject(p, "state_off", "OFF");
        cJSON_Delete(ct);
        publish_disco("switch", "on_off", p);
    }

    /* --- Button Reset alarme --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Reset alarme");
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "%s/cmd/reset_alarm", sub_topic_prefix);
        cJSON_AddStringToObject(p, "command_topic", cmd);
        cJSON_AddStringToObject(p, "payload_press", "1");
        publish_disco("button", "reset_alarm", p);
    }

    /* --- Number Setpoint T° --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Consigne température");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.setpoint }}");
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "%s/cmd/setpoint", sub_topic_prefix);
        cJSON_AddStringToObject(p, "command_topic", cmd);
        cJSON_AddNumberToObject(p, "min", 10);
        cJSON_AddNumberToObject(p, "max", 30);
        cJSON_AddNumberToObject(p, "step", 1);
        cJSON_AddStringToObject(p, "unit_of_measurement", "°C");
        cJSON_AddStringToObject(p, "mode", "slider");
        publish_disco("number", "setpoint", p);
    }

    /* --- Select puissance 1..5 --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Niveau puissance");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.power|string }}");
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "%s/cmd/power", sub_topic_prefix);
        cJSON_AddStringToObject(p, "command_topic", cmd);
        cJSON *opts = cJSON_CreateArray();
        for (int i = 1; i <= 5; i++) {
            char s[4]; snprintf(s, sizeof(s), "%d", i);
            cJSON_AddItemToArray(opts, cJSON_CreateString(s));
        }
        cJSON_AddItemToObject(p, "options", opts);
        publish_disco("select", "power_level_cmd", p);
    }

    /* Also publish availability=online now that discovery is out. */
    esp_mqtt_client_publish(client, avail_topic, "online", 0, 1, 1);

    ESP_LOGI(TAG, "HA discovery published (11 entities)");
    return ESP_OK;
}
