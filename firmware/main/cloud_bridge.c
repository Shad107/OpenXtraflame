/**
 * cloud_bridge - client MQTT vers cloud Extraflame (=Omnyvore).
 *
 * TARGET_BLACKLABEL uniquement. Reverse complet 2026-07-08 :
 *  - Broker : mqtts://mqtt.extraflame.it:8883 avec CA Omnyvore embedded
 *  - Auth   : client_id=MAC uppercase, username=matricola, password=secure_code
 *  - MQTT   : 3.1.1 (=5.0 refusé sans properties Omnyvore custom)
 *
 * Format topic exact :
 *   omv/ex/<MAC>/<model> 1.8/<matricola>/{IN|OUT|REPLY}/<family>
 *
 * (SPACE réel dans le path entre model et version.)
 *
 * Le stove_model REEL Omnyvore (=ex "001275002000") est récupéré via
 * l'API REST appapi.extraflame.it avec les credentials TotalControl 2
 * du user. Cf. cloud_rest.c
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "cloud_bridge.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#ifdef TARGET_BLACKLABEL
#include "micronova.h"
#include "cloud_rest.h"
#include "config_nvs.h"
#endif

static const char *TAG = "CLOUD";
static esp_mqtt_client_handle_t cloud_client = NULL;
static cloud_bridge_stats_t s_stats = {0};

#define CLOUD_BROKER_URI  "mqtts://mqtt.extraflame.it:8883"

extern const uint8_t extraflame_ca_pem_start[] asm("_binary_extraflame_ca_pem_start");
extern const uint8_t extraflame_ca_pem_end[]   asm("_binary_extraflame_ca_pem_end");

/* Segments dynamiques du topic - remplis au start */
static char s_mac_upper[16]    = "";
static char s_matricola[16]    = "";
static char s_stove_model[16]  = "";   /* modèle Omnyvore (=ex "001275002000") */
#define FW_VERSION_STR   "1.8"

/* Périodicité publish OUT (=publie state toutes les 30s) */
#define PUBLISH_PERIOD_MS  30000
static TaskHandle_t s_pub_task = NULL;

/* --------- helpers topic builder --------- */

/* Construit "omv/ex/<MAC>/<model> 1.8/<matricola>/<DIR>/<family>" */
static void build_topic(char *out, size_t out_sz, const char *dir, const char *family)
{
    snprintf(out, out_sz, "omv/ex/%s/%s %s/%s/%s/%s",
             s_mac_upper, s_stove_model, FW_VERSION_STR,
             s_matricola, dir, family);
}

/* --------- handlers messages IN --------- */

/* Publie REPLY sur le topic replyto avec correlationid pour un ack MQTT-style */
static void publish_reply(const char *reply_topic, const char *correlation_id,
                           const char *payload_json)
{
    if (!cloud_client || !s_stats.connected) return;
    if (!reply_topic || !correlation_id) return;
    char reply[512];
    snprintf(reply, sizeof(reply),
             "{\"correlationid\":\"%s\",\"timestamp\":%" PRIu64 ",%s}",
             correlation_id, (uint64_t)(esp_log_timestamp() / 1000),
             payload_json ? payload_json : "\"ok\":true");
    esp_mqtt_client_publish(cloud_client, reply_topic, reply, 0, 0, 0);
}

#ifdef TARGET_BLACKLABEL
/* Exécute une commande IN/settings reçue du cloud. Le firmware original
 * accepte : machineState, targetPower, targetRoomTemp, targetWaterTemp,
 *          mainFanSpeed, mainFanMode, etc.
 * On mappe vers les registres EEPROM connus. */
static void handle_in_settings(cJSON *o)
{
    cJSON *v;

    v = cJSON_GetObjectItem(o, "machineState");
    if (cJSON_IsNumber(v)) {
        /* Cf mqtt_bridge.c handle_command : STOVE_STATE=0x01 ON, =0x06 OFF */
        uint8_t stcmd = ((int)v->valuedouble == 0) ? 0x06 : 0x01;
        mn_write_register(MN_RAM_STOVE_STATE, stcmd);
    }
    v = cJSON_GetObjectItem(o, "targetPower");
    if (cJSON_IsNumber(v)) {
        mn_write_register(MN_EEP_POWER_SET_IVENT, (uint8_t)v->valuedouble);
    }
    v = cJSON_GetObjectItem(o, "targetRoomTemp");
    if (cJSON_IsNumber(v)) {
        mn_write_register(MN_EEP_TEMP_SET_IVENT, (uint8_t)v->valuedouble);
    }
}
#endif

static void on_data(esp_mqtt_event_handle_t event)
{
    if (!event || event->topic_len <= 0 || event->data_len <= 0) return;
    char topic[192], payload[512];
    size_t tlen = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
    memcpy(topic, event->topic, tlen); topic[tlen] = '\0';
    size_t plen = event->data_len < (int)sizeof(payload) - 1 ? event->data_len : (int)sizeof(payload) - 1;
    memcpy(payload, event->data, plen); payload[plen] = '\0';
    ESP_LOGI(TAG, "IN %s : %s", topic, payload);

    cJSON *o = cJSON_Parse(payload);
    if (!o) return;
    cJSON *cid = cJSON_GetObjectItem(o, "correlationid");
    cJSON *rt  = cJSON_GetObjectItem(o, "replyto");

#ifdef TARGET_BLACKLABEL
    /* Route par family = dernier segment du topic */
    const char *family = strrchr(topic, '/');
    if (family) family++;
    if (family && strcmp(family, "settings") == 0) {
        handle_in_settings(o);
    }
#endif

    /* Ack REPLY */
    if (cJSON_IsString(cid) && cJSON_IsString(rt)) {
        publish_reply(rt->valuestring, cid->valuestring, "\"applied\":true");
    }
    cJSON_Delete(o);
}

/* --------- Publish OUT/status + OUT/temperature périodique --------- */

#ifdef TARGET_BLACKLABEL
static void pub_task(void *_)
{
    ESP_LOGI(TAG, "pub_task started, period=%d ms", PUBLISH_PERIOD_MS);
    /* Payload assez gros pour toutes les mesures ; sur la HEAP pour éviter
     * un stack overflow silencieux (=snapshot ~200 bytes déjà local). */
    static char topic[192];
    static char payload[512];
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(PUBLISH_PERIOD_MS));
        if (!cloud_client || !s_stats.connected) {
            ESP_LOGD(TAG, "pub_task skip: connected=%d", s_stats.connected);
            continue;
        }
        mn_stove_state_snapshot_t snap;
        mn_get_snapshot(&snap);
        /* Pas de timestamp : esp_log_timestamp() donne ms depuis boot, pas
         * unix epoch. Le server assigne new Date().getTime() si absent. */

        build_topic(topic, sizeof(topic), "OUT", "temperature");
        snprintf(payload, sizeof(payload),
                 "{\"roomTemp\":%.1f,\"smokeTemp\":%.0f}",
                 snap.t_ambient, snap.t_smoke);
        int r = esp_mqtt_client_publish(cloud_client, topic, payload, 0, 1, 0);
        ESP_LOGI(TAG, "PUB %s -> mid=%d", topic, r);

        build_topic(topic, sizeof(topic), "OUT", "status");
        snprintf(payload, sizeof(payload),
                 "{\"machineState\":%d,\"targetPower\":%d,\"targetRoomTemp\":%d,\"power\":%d,\"alarmCode\":%d}",
                 (int)snap.state, snap.power_level, (int)snap.set_temperature,
                 snap.power_real, snap.alarm_code);
        esp_mqtt_client_publish(cloud_client, topic, payload, 0, 1, 0);

        /* OUT/settings pour que targetPower/targetRoomTemp remontent en app.
         * Le server responsepattern parse ces keys depuis ce channel. */
        build_topic(topic, sizeof(topic), "OUT", "settings");
        snprintf(payload, sizeof(payload),
                 "{\"machineState\":%d,\"targetPower\":%d,\"targetRoomTemp\":%d}",
                 (int)snap.state, snap.power_level, (int)snap.set_temperature);
        esp_mqtt_client_publish(cloud_client, topic, payload, 0, 1, 0);

        build_topic(topic, sizeof(topic), "OUT", "workingtimers");
        snprintf(payload, sizeof(payload),
                 "{\"h_total\":%d,\"starts\":%d,\"h_p1\":%d,\"h_p2\":%d,\"h_p3\":%d,\"h_p4\":%d,\"h_p5\":%d}",
                 snap.hours_total, snap.starts_total,
                 snap.hours_p1, snap.hours_p2, snap.hours_p3, snap.hours_p4, snap.hours_p5);
        esp_mqtt_client_publish(cloud_client, topic, payload, 0, 1, 0);
    }
}
#endif

/* --------- MQTT event handler --------- */

static void subscribe_all_in(esp_mqtt_client_handle_t c)
{
    static const char *FAMILIES[] = {
        "settings", "time", "crono", "firmware", "misc", "addr"
    };
    char topic[192];
    for (size_t i = 0; i < sizeof(FAMILIES) / sizeof(FAMILIES[0]); i++) {
        build_topic(topic, sizeof(topic), "IN", FAMILIES[i]);
        esp_mqtt_client_subscribe(c, topic, 0);
        ESP_LOGI(TAG, "SUB %s", topic);
    }
}

static void cloud_event_handler(void *arg, esp_event_base_t base,
                                 int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Cloud MQTT connected");
        s_stats.connected = true;
        s_stats.connect_count++;
        snprintf(s_stats.last_error_str, sizeof(s_stats.last_error_str), "connected");
        if (s_stove_model[0]) {
            subscribe_all_in(event->client);
#ifdef TARGET_BLACKLABEL
            if (!s_pub_task) {
                xTaskCreate(pub_task, "cloud_pub", 4096, NULL, 5, &s_pub_task);
            }
#endif
        } else {
            ESP_LOGW(TAG, "stove_model vide, pas de SUB (=faut fetch REST d'abord)");
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Cloud MQTT disconnected");
        s_stats.connected = false;
        break;
    case MQTT_EVENT_DATA:
        on_data(event);
        break;
    case MQTT_EVENT_ERROR:
        s_stats.error_count++;
        if (event && event->error_handle) {
            s_stats.last_error = event->error_handle->esp_tls_last_esp_err;
            snprintf(s_stats.last_error_str, sizeof(s_stats.last_error_str),
                     "type=%d tls=0x%x flags=0x%x stack=0x%x conn=%d",
                     event->error_handle->error_type,
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_tls_cert_verify_flags,
                     event->error_handle->esp_tls_stack_err,
                     event->error_handle->connect_return_code);
        }
        ESP_LOGE(TAG, "Cloud MQTT error: %s", s_stats.last_error_str);
        break;
    default:
        break;
    }
    (void)arg; (void)base;
}

/* --------- helper : construit MAC uppercase depuis WiFi STA --------- */
static void fetch_mac_upper(void)
{
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(s_mac_upper, sizeof(s_mac_upper),
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t cloud_bridge_start(const app_config_t *cfg)
{
    strncpy(s_stats.broker_uri, CLOUD_BROKER_URI, sizeof(s_stats.broker_uri)-1);
    s_stats.enabled = cfg && cfg->cloud_enabled;
    if (!cfg || !cfg->cloud_enabled) {
        ESP_LOGI(TAG, "Cloud bridge disabled");
        snprintf(s_stats.last_error_str, sizeof(s_stats.last_error_str), "cloud_enabled=false");
        return ESP_OK;
    }
    if (cloud_client) return ESP_OK;

#ifdef TARGET_BLACKLABEL
    const char *matricola = mn_get_stove_matricola();
    if (!matricola || !matricola[0]) {
        ESP_LOGE(TAG, "matricola vide, abort");
        snprintf(s_stats.last_error_str, sizeof(s_stats.last_error_str), "matricola empty");
        return ESP_ERR_INVALID_STATE;
    }
    strncpy(s_matricola, matricola, sizeof(s_matricola) - 1);
    strncpy(s_stats.matricola, matricola, sizeof(s_stats.matricola) - 1);
    const char *secure_code = mn_get_stove_secure_code();
    if (!secure_code || !secure_code[0]) {
        ESP_LOGE(TAG, "secure_code vide, abort");
        snprintf(s_stats.last_error_str, sizeof(s_stats.last_error_str), "secure_code empty");
        return ESP_ERR_INVALID_STATE;
    }
    fetch_mac_upper();

    /* stove_model : depuis cfg.tc2_stove_model si cached, sinon fetch REST */
    if (cfg->tc2_stove_model[0]) {
        strncpy(s_stove_model, cfg->tc2_stove_model, sizeof(s_stove_model) - 1);
        ESP_LOGI(TAG, "stove_model depuis cache NVS: %s", s_stove_model);
    } else if (cfg->tc2_username[0] && cfg->tc2_password[0]) {
        ESP_LOGI(TAG, "Fetch REST TotalControl 2 pour stove_model...");
        char sid[40] = "", smod[16] = "";
        if (cloud_rest_fetch_stove_info(cfg->tc2_username, cfg->tc2_password,
                                         matricola, sid, sizeof(sid),
                                         smod, sizeof(smod)) == ESP_OK) {
            if (smod[0]) {
                strncpy(s_stove_model, smod, sizeof(s_stove_model) - 1);
                ESP_LOGI(TAG, "stove_model récupéré: %s", s_stove_model);
                /* Persist ces valeurs dans NVS pour reboots suivants */
                app_config_t *mut = (app_config_t *)cfg;
                strncpy(mut->tc2_stove_id, sid, sizeof(mut->tc2_stove_id) - 1);
                strncpy(mut->tc2_stove_model, smod, sizeof(mut->tc2_stove_model) - 1);
                config_nvs_save(mut);
            }
        } else {
            ESP_LOGW(TAG, "Fetch REST failed, on connect quand meme sans SUB");
        }
    } else {
        ESP_LOGW(TAG, "Aucun tc2_username/password renseigné, SUB impossible");
    }

    ESP_LOGI(TAG, "Cloud starting : cid=%s user=%s (topic base = omv/ex/%s/%s %s/%s/...)",
             s_mac_upper, matricola, s_mac_upper, s_stove_model, FW_VERSION_STR, matricola);

    esp_mqtt_client_config_t cfg_mqtt = {
        .broker.address.uri            = CLOUD_BROKER_URI,
        .broker.verification.certificate = (const char *)extraflame_ca_pem_start,
        .credentials.client_id         = s_mac_upper,
        .credentials.username          = matricola,
        .credentials.authentication.password = secure_code,
        .session.keepalive             = 60,
        .session.protocol_ver          = MQTT_PROTOCOL_V_3_1_1,
    };
    cloud_client = esp_mqtt_client_init(&cfg_mqtt);
    if (!cloud_client) {
        snprintf(s_stats.last_error_str, sizeof(s_stats.last_error_str), "init NULL");
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(cloud_client, ESP_EVENT_ANY_ID,
                                    cloud_event_handler, NULL);
    s_stats.started = true;
    snprintf(s_stats.last_error_str, sizeof(s_stats.last_error_str), "starting...");
    return esp_mqtt_client_start(cloud_client);
#else
    ESP_LOGW(TAG, "Cloud bridge only supported on TARGET_BLACKLABEL");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t cloud_bridge_stop(void)
{
    if (s_pub_task) { vTaskDelete(s_pub_task); s_pub_task = NULL; }
    if (!cloud_client) return ESP_OK;
    esp_mqtt_client_stop(cloud_client);
    esp_mqtt_client_destroy(cloud_client);
    cloud_client = NULL;
    s_stats.connected = false;
    s_stats.started = false;
    return ESP_OK;
}

bool cloud_bridge_connected(void) { return s_stats.connected; }

void cloud_bridge_get_stats(cloud_bridge_stats_t *out) { if (out) *out = s_stats; }
