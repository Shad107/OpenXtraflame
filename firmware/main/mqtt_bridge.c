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
#include "alarm_history.h"
#include "ota.h"
#include "cloud_bridge.h"
#include "config_nvs.h"
#include "mqtt_client.h"
#include "esp_system.h"
#include "esp_timer.h"
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
    /* Reset maintenance counters : snapshot h_total / starts_total à l'instant T */
    if (strstr(topic, "/cmd/reset_service")) {
        mn_stove_state_snapshot_t s; mn_get_snapshot(&s);
        local_cfg.maint_service_h_at_reset = s.hours_total;
        config_nvs_save(&local_cfg);
        ESP_LOGI(TAG, "reset_service : h_at_reset=%d", s.hours_total);
        return;
    }
    if (strstr(topic, "/cmd/reset_cleaning")) {
        mn_stove_state_snapshot_t s; mn_get_snapshot(&s);
        local_cfg.maint_cleaning_starts_at_reset = s.starts_total;
        config_nvs_save(&local_cfg);
        ESP_LOGI(TAG, "reset_cleaning : starts_at_reset=%d", s.starts_total);
        return;
    }
    if (strstr(topic, "/cmd/rollback_firmware")) {
        ESP_LOGW(TAG, "MQTT rollback_firmware demandé → bascule ota_1");
        ota_rollback();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return;
    }
#ifdef TARGET_BLACKLABEL
    if (strstr(topic, "/cmd/cloud_toggle")) {
        char buf[8] = {0};
        if (dlen >= sizeof(buf)) return;
        memcpy(buf, data, dlen);
        bool on = (buf[0] == '1' || strstr(buf, "ON") || strstr(buf, "on") || strstr(buf, "true"));
        /* Toggle uniquement si login/mdp TC2 configurés (=sinon cloud n'a rien
         * pour se connecter). Le switch HA discovery est aussi conditionnel
         * dessus, donc en théorie on est déjà safe côté client. */
        if (on && !(local_cfg.tc2_username[0] && local_cfg.tc2_password[0])) {
            ESP_LOGW(TAG, "cloud_toggle=ON ignoré : login/mdp TC2 non configurés");
            return;
        }
        if (local_cfg.cloud_enabled == on) return;
        local_cfg.cloud_enabled = on;
        config_nvs_save(&local_cfg);
        ESP_LOGI(TAG, "cloud_enabled = %s → reboot pour appliquer", on ? "ON" : "OFF");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
        return;
    }
#endif
    if (strstr(topic, "/cmd/setpoint")) {
        char buf[8] = {0};
        if (dlen < sizeof(buf)) {
            memcpy(buf, data, dlen);
            int v = atoi(buf);
            /* Écrit dans EEPROM_SET_AMB (=0x7D I_VENT) = source persistante.
             * Bank 1 encoded dans reg_addr → opcode 0xA0 EEPROM write. */
            mn_write_register(MN_EEP_TEMP_SET_IVENT, (uint8_t)v);
        }
        return;
    }
    if (strstr(topic, "/cmd/power")) {
        char buf[4] = {0};
        if (dlen < sizeof(buf)) {
            memcpy(buf, data, dlen);
            int v = atoi(buf);
            /* Écrit dans EEPROM_SET_POWER (=0x7F I_VENT). Cf setpoint ci-dessus. */
            mn_write_register(MN_EEP_POWER_SET_IVENT, (uint8_t)v);
        }
        return;
    }
    /* Chrono commands */
    if (strstr(topic, "/cmd/chrono_master")) {
        char buf[8] = {0};
        if (dlen < sizeof(buf)) {
            memcpy(buf, data, dlen);
            uint8_t v = (strstr(buf, "ON") || buf[0]=='1' || strstr(buf, "true")) ? 1 : 0;
            mn_write_register(MN_EEP_CHRONO_ENABLE, v);
        }
        return;
    }
    /* /cmd/chrono_progN_field with N=1..4 and field=enable|start|stop|temp */
    const char *cp = strstr(topic, "/cmd/chrono_prog");
    if (cp) {
        cp += strlen("/cmd/chrono_prog");
        if (*cp < '1' || *cp > '4') { ESP_LOGW(TAG, "bad prog id"); return; }
        int idx = *cp - '1';  /* 0..3 */
        static const uint16_t base_addr[4] = {
            MN_EEP_CHRONO1_START, MN_EEP_CHRONO2_START,
            MN_EEP_CHRONO3_START, MN_EEP_CHRONO4_START
        };
        static const uint16_t en_addr[4] = {
            MN_EEP_CHRONO_EN1, MN_EEP_CHRONO_EN2, MN_EEP_CHRONO_EN3, MN_EEP_CHRONO_EN4
        };
        char buf[16] = {0};
        if (dlen >= sizeof(buf)) return;
        memcpy(buf, data, dlen);
        if (strstr(cp, "_enable")) {
            uint8_t v = (strstr(buf, "ON") || buf[0]=='1' || strstr(buf, "true")) ? 1 : 0;
            mn_write_register(en_addr[idx], v);
        } else if (strstr(cp, "_start") || strstr(cp, "_stop")) {
            int h = 0, m2 = 0;
            if (sscanf(buf, "%d:%d", &h, &m2) == 2) {
                uint8_t raw = (uint8_t)((h * 60 + m2) / 10);
                uint16_t addr = base_addr[idx] + (strstr(cp, "_start") ? 0 : 1);
                mn_write_register(addr, raw);
            }
        } else if (strstr(cp, "_temp")) {
            mn_write_register(base_addr[idx] + 9, (uint8_t)atoi(buf));
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
    /* Use auto-detected type (=Phase 3) - s.stove_type reflects real hardware */
    stove_type_t st = s.stove_type;
    if (st == STOVE_TYPE_I_CALD ||
        st == STOVE_TYPE_I_IDRO ||
        st == STOVE_TYPE_I_IDRO_2) {
        cJSON_AddNumberToObject(o, "t_water", s.t_water);
    }
    cJSON_AddNumberToObject(o, "t_smoke",   s.t_smoke);
    cJSON_AddNumberToObject(o, "setpoint",  s.set_temperature);
    cJSON_AddNumberToObject(o, "hours_total",  s.hours_total);
    cJSON_AddNumberToObject(o, "starts_total", s.starts_total);
    cJSON_AddNumberToObject(o, "hours_p1", s.hours_p1);
    cJSON_AddNumberToObject(o, "hours_p2", s.hours_p2);
    cJSON_AddNumberToObject(o, "hours_p3", s.hours_p3);
    cJSON_AddNumberToObject(o, "hours_p4", s.hours_p4);
    cJSON_AddNumberToObject(o, "hours_p5", s.hours_p5);
    cJSON_AddNumberToObject(o, "pellets_total_kg",         s.pellets_total_kg);
    cJSON_AddNumberToObject(o, "pellets_since_refill_kg",  s.pellets_since_refill_kg);
    cJSON_AddNumberToObject(o, "pellets_remaining_kg",     s.pellets_remaining_kg);
    cJSON_AddNumberToObject(o, "pellets_cost_lifetime_eur", s.pellets_cost_lifetime_eur);
    cJSON_AddNumberToObject(o, "pellets_days_left",         s.pellets_days_left);
    cJSON_AddNumberToObject(o, "pellets_kg_per_day",        s.pellets_kg_per_day);
    cJSON_AddNumberToObject(o, "pellets_empty_ts",          s.pellets_empty_ts);
    /* Alarmes décomposées bit par bit (=8 bits nommés du registre RAM_ALLARM) */
    cJSON_AddBoolToObject(o, "alarm_sonda_fumi",    s.alarm_sonda_fumi);
    cJSON_AddBoolToObject(o, "alarm_hot_fumi",      s.alarm_hot_fumi);
    cJSON_AddBoolToObject(o, "alarm_fumi_corto",    s.alarm_fumi_corto);
    cJSON_AddBoolToObject(o, "alarm_aspiratore",    s.alarm_aspiratore);
    cJSON_AddBoolToObject(o, "alarm_no_accensione", s.alarm_no_accensione);
    cJSON_AddBoolToObject(o, "alarm_no_fiamma",     s.alarm_no_fiamma);
    cJSON_AddBoolToObject(o, "alarm_depression",    s.alarm_depression);
    cJSON_AddBoolToObject(o, "alarm_coclea_cmd",    s.alarm_coclea_cmd);
    /* Maintenance : compteurs avant service/nettoyage */
    cJSON_AddNumberToObject(o, "hours_since_service",     s.hours_since_service);
    cJSON_AddNumberToObject(o, "hours_before_service",    s.hours_before_service);
    cJSON_AddNumberToObject(o, "starts_since_cleaning",   s.starts_since_cleaning);
    cJSON_AddNumberToObject(o, "starts_before_cleaning",  s.starts_before_cleaning);
    /* Trémie + modulation + cause arrêt (=I_VENT Addrs_dyn reverse) */
    cJSON_AddBoolToObject(o,   "tremie_vide",             s.tremie_vide);
    cJSON_AddNumberToObject(o, "modulation",              s.modulation);
    cJSON_AddNumberToObject(o, "causa_stato7",            s.causa_stato7);
#ifdef TARGET_BLACKLABEL
    /* Cloud state pour HA discovery / debug (=blacklabel only) */
    {
        cloud_bridge_stats_t cs;
        cloud_bridge_get_stats(&cs);
        cJSON_AddBoolToObject(o, "cloud_enabled",   cs.enabled);
        cJSON_AddBoolToObject(o, "cloud_connected", cs.connected);
    }
#endif
    /* Uptime en secondes. HA peut dériver last_boot via template
     * "{{ (now() - timedelta(seconds=value)).isoformat() }}". */
    cJSON_AddNumberToObject(o, "uptime_s",
        (double)(esp_timer_get_time() / 1000000LL));
    /* Safe mode next boot flag (=visible HA pour info) */
    cJSON_AddBoolToObject(o, "safe_mode_next", local_cfg.safe_mode_next_boot);
    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);

    char topic[160];
    snprintf(topic, sizeof(topic), "%s/state", sub_topic_prefix);
    esp_mqtt_client_publish(client, topic, json, 0, 0, 0);
    free(json);

    /* Chrono : publie séparément sur <prefix>/chrono (=stable, changement rare) */
    char *cjson = mn_chrono_json();
    if (cjson) {
        char chrono_topic[160];
        snprintf(chrono_topic, sizeof(chrono_topic), "%s/chrono", sub_topic_prefix);
        esp_mqtt_client_publish(client, chrono_topic, cjson, 0, 0, 1);  /* retained */
        free(cjson);
    }

    /* Historique alarmes (=ring buffer 20 dernières, retained) */
    {
        char *ajson = alarm_history_dump_json();
        if (ajson) {
            char h_topic[160];
            snprintf(h_topic, sizeof(h_topic), "%s/history_alarms", sub_topic_prefix);
            /* HA json_attributes_topic n'aime pas les arrays racine =wrap {events:[]} */
            char *wrap = malloc(strlen(ajson) + 32);
            if (wrap) {
                int wn = sprintf(wrap, "{\"events\":%s,\"count\":%u}", ajson,
                                 (unsigned)alarm_history_count());
                esp_mqtt_client_publish(client, h_topic, wrap, wn, 1, 1);  /* qos1 retained */
                free(wrap);
            }
            free(ajson);
        }
    }

    /* Params tech UT04 : lecture directe des 32 registres 0x40-0x5F.
     * On expose que les non-zéro pour rester compact. */
    {
        struct { uint8_t pr; uint8_t addr; uint16_t factory; } TABLE[] = {
            {1,0x40,15},{2,0x41,6},{3,0x42,60},{4,0x43,19},{5,0x44,20},
            {6,0x45,19},{7,0x46,22},{8,0x47,29},{9,0x48,35},{10,0x49,45},
            {11,0x4A,240},{12,0x4B,30},{13,0x4C,50},{14,0x4D,260},{15,0x4E,100},
            {23,0x56,12},{24,0x57,15},{25,0x58,17},{26,0x59,19},{27,0x5A,21},
        };
        int n_items = sizeof(TABLE)/sizeof(TABLE[0]);
        char *buf = malloc(2048);
        if (buf) {
            int off = 0, divergent = 0;
            off += sprintf(buf+off, "{\"params\":[");
            for (int i = 0; i < n_items; i++) {
                uint8_t v = mn_get_ram(0x100 + TABLE[i].addr);
                int diverges = (v != 0) && ((v > TABLE[i].factory * 130 / 100) ||
                                            (v < TABLE[i].factory * 70 / 100));
                if (diverges) divergent++;
                off += sprintf(buf+off, "%s{\"pr\":%u,\"addr\":%u,\"val\":%u,\"factory\":%u,\"div\":%s}",
                    i ? "," : "", TABLE[i].pr, TABLE[i].addr, v, TABLE[i].factory,
                    diverges ? "true" : "false");
            }
            off += sprintf(buf+off, "],\"divergent_count\":%d}", divergent);
            char t_topic[160];
            snprintf(t_topic, sizeof(t_topic), "%s/params_tech", sub_topic_prefix);
            esp_mqtt_client_publish(client, t_topic, buf, off, 1, 1);
            free(buf);
        }
    }

    /* Diagnostic combustion (=règles data-driven simples) */
    {
        char *diag = malloc(1024);
        if (diag) {
            int off = 0, sev_max = 0;
            const char *sevs[] = {"ok", "info", "warning", "critical"};
            off += sprintf(diag+off, "{\"diagnostics\":[");
            int first = 1;

            /* Nettoyage brasero fréquent */
            uint8_t pr03 = mn_get_ram(0x100 + 0x42);
            if (pr03 && pr03 < 40) {
                off += sprintf(diag+off, "%s{\"code\":\"pr03_frequent\",\"sev\":2,"
                    "\"msg\":\"Nettoyage brasero %umin (defaut 60)\","
                    "\"reco\":\"Corriger Pr04-Pr08 coclea puis remonter Pr03\"}",
                    first ? "" : ",", pr03);
                first = 0;
                if (sev_max < 2) sev_max = 2;
            }
            /* Coclea sous factory */
            uint8_t pr04 = mn_get_ram(0x100 + 0x43);
            uint8_t pr05 = mn_get_ram(0x100 + 0x44);
            uint8_t pr08 = mn_get_ram(0x100 + 0x47);
            if (pr04 && pr05 && pr08 &&
                (pr04 < 19*40/100 || pr05 < 20*40/100 || pr08 < 29*40/100)) {
                off += sprintf(diag+off, "%s{\"code\":\"coclea_low\",\"sev\":3,"
                    "\"msg\":\"Coclea Pr04-Pr08 <40%% factory = combustion maigre\","
                    "\"reco\":\"Remonter progressivement +30%% par etape 24h\"}",
                    first ? "" : ",");
                first = 0;
                sev_max = 3;
            }
            /* T fumees */
            mn_stove_state_snapshot_t sc; mn_get_snapshot(&sc);
            if (sc.t_smoke > 280) {
                off += sprintf(diag+off, "%s{\"code\":\"smoke_hot\",\"sev\":2,"
                    "\"msg\":\"T fumees %.0f>280C, trop d'air excedent\","
                    "\"reco\":\"Baisser aspiration Pr16-Pr22\"}", first ? "" : ",", (double)sc.t_smoke);
                first = 0;
                if (sev_max < 2) sev_max = 2;
            } else if (sc.t_smoke > 50 && sc.t_smoke < 130 && sc.state >= 3 && sc.state <= 4) {
                off += sprintf(diag+off, "%s{\"code\":\"smoke_cold\",\"sev\":2,"
                    "\"msg\":\"T fumees %.0f<130C en marche, risque condensation\","
                    "\"reco\":\"Verifier etancheite + augmenter aspiration\"}", first ? "" : ",", (double)sc.t_smoke);
                first = 0;
                if (sev_max < 2) sev_max = 2;
            }
            /* Alarme active */
            if (sc.alarm_code != 0) {
                off += sprintf(diag+off, "%s{\"code\":\"alarm_active\",\"sev\":3,"
                    "\"msg\":\"Alarme 0x%02x active\","
                    "\"reco\":\"Voir historique + resoudre cause\"}", first ? "" : ",", sc.alarm_code);
                first = 0;
                sev_max = 3;
            }

            off += sprintf(diag+off, "],\"severity\":\"%s\"}", sevs[sev_max]);
            char d_topic[160];
            snprintf(d_topic, sizeof(d_topic), "%s/combustion_diag", sub_topic_prefix);
            esp_mqtt_client_publish(client, d_topic, diag, off, 1, 1);
            free(diag);
        }
    }

    /* Config pellet : publie sur <prefix>/pellet (=retained, changement rare) */
    {
        cJSON *pl = cJSON_CreateObject();
        cJSON_AddNumberToObject(pl, "tank_capacity_kg", local_cfg.pellet_tank_capacity_kg);
        cJSON_AddNumberToObject(pl, "sack_size_kg",     local_cfg.pellet_sack_size_kg);
        cJSON_AddNumberToObject(pl, "price_per_sack",   local_cfg.pellet_price_per_sack_eur);
        cJSON_AddNumberToObject(pl, "winter_days",      local_cfg.pellet_winter_days);
        cJSON_AddNumberToObject(pl, "consumption_p1",   local_cfg.pellet_consumption_p1);
        cJSON_AddNumberToObject(pl, "consumption_p2",   local_cfg.pellet_consumption_p2);
        cJSON_AddNumberToObject(pl, "consumption_p3",   local_cfg.pellet_consumption_p3);
        cJSON_AddNumberToObject(pl, "consumption_p4",   local_cfg.pellet_consumption_p4);
        cJSON_AddNumberToObject(pl, "consumption_p5",   local_cfg.pellet_consumption_p5);
        char *pjson = cJSON_PrintUnformatted(pl);
        cJSON_Delete(pl);
        if (pjson) {
            char pt[160];
            snprintf(pt, sizeof(pt), "%s/pellet", sub_topic_prefix);
            esp_mqtt_client_publish(client, pt, pjson, 0, 0, 1);  /* retained */
            free(pjson);
        }
    }
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
    cJSON_AddStringToObject(dev, "manufacturer", "isno.fr");
    cJSON_AddStringToObject(dev, "model",        "OpenXtraflame Black Label");
    cJSON_AddStringToObject(dev, "configuration_url", "https://www.isno.fr/projets/openxtraflame");
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
    /* Use auto-detected type (=Phase 3) - reflects real hardware, not user config.
     * Si non-hydro : publish EMPTY retained message pour supprimer la sensor t_water
     * précédemment retained côté HA (=nettoyage discovery). */
    stove_type_t st_disco = mn_detected_stove_type();
    if (st_disco == STOVE_TYPE_I_CALD ||
        st_disco == STOVE_TYPE_I_IDRO ||
        st_disco == STOVE_TYPE_I_IDRO_2) {
        publish_disco("sensor", "t_water",
                      sensor("Température eau",  "t_water",   "°C", "temperature"));
    } else {
        /* Delete retained discovery for t_water on non-hydro stoves */
        char t[192];
        snprintf(t, sizeof(t), "homeassistant/sensor/%s/t_water/config", device_id);
        esp_mqtt_client_publish(client, t, "", 0, 1, 1);  /* retained empty = delete */
    }
    publish_disco("sensor", "power_level",
                  sensor("Puissance",            "power",     NULL, NULL));
    publish_disco("sensor", "power_real",
                  sensor("Puissance réelle",     "power_real", NULL, NULL));
    publish_disco("sensor", "alarm_code",
                  sensor("Code alarme",          "alarm",     NULL, NULL));
    /* Compteurs maintenance */
    #define DIAG(obj_id, name_str, key, unit_str) do { \
        cJSON *p = sensor(name_str, key, unit_str, NULL); \
        cJSON_AddStringToObject(p, "entity_category", "diagnostic"); \
        publish_disco("sensor", obj_id, p); \
    } while(0)
    DIAG("hours_total",  "Heures totales",     "hours_total",  "h");
    DIAG("starts_total", "Nombre démarrages",  "starts_total", NULL);
    DIAG("hours_p1",     "Heures P1",          "hours_p1",     "h");
    DIAG("hours_p2",     "Heures P2",          "hours_p2",     "h");
    DIAG("hours_p3",     "Heures P3",          "hours_p3",     "h");
    DIAG("hours_p4",     "Heures P4",          "hours_p4",     "h");
    DIAG("hours_p5",     "Heures P5",          "hours_p5",     "h");
    #undef DIAG

    /* Sensors pellets (=readonly, section Contrôles). Templates arrondissent
     * les valeurs à afficher : kg à 0 décimales, € et kg restants à 2. */
    {
        cJSON *p = sensor("Pellets consommés", "pellets_total_kg", "kg", "weight");
        cJSON_ReplaceItemInObject(p, "value_template",
            cJSON_CreateString("{{ value_json.pellets_total_kg | round(0) }}"));
        publish_disco("sensor", "pellets_total_kg", p);
    }
    {
        cJSON *p = sensor("Coût pellets lifetime", "pellets_cost_lifetime_eur", "€", "monetary");
        cJSON_ReplaceItemInObject(p, "value_template",
            cJSON_CreateString("{{ value_json.pellets_cost_lifetime_eur | round(2) }}"));
        publish_disco("sensor", "pellets_cost_lifetime", p);
    }
    {
        cJSON *p = sensor("Pellets restants", "pellets_remaining_kg", "kg", "weight");
        cJSON_ReplaceItemInObject(p, "value_template",
            cJSON_CreateString("{{ value_json.pellets_remaining_kg | round(1) }}"));
        publish_disco("sensor", "pellets_remaining_kg", p);
    }
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Pellets conso journalière");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{{ value_json.pellets_kg_per_day | round(2) }}");
        cJSON_AddStringToObject(p, "unit_of_measurement", "kg/j");
        cJSON_AddStringToObject(p, "icon", "mdi:chart-line");
        publish_disco("sensor", "pellets_kg_per_day", p);
    }
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Pellets jours restants");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{{ value_json.pellets_days_left | round(1) }}");
        cJSON_AddStringToObject(p, "unit_of_measurement", "j");
        cJSON_AddStringToObject(p, "icon", "mdi:calendar-clock");
        publish_disco("sensor", "pellets_days_left", p);
    }
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Trémie vide prévue");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "device_class", "timestamp");
        cJSON_AddStringToObject(p, "value_template",
            "{% if value_json.pellets_empty_ts and value_json.pellets_empty_ts > 0 %}"
            "{{ value_json.pellets_empty_ts | as_datetime | as_local }}"
            "{% else %}unknown{% endif %}");
        cJSON_AddStringToObject(p, "icon", "mdi:tank-outline");
        publish_disco("sensor", "pellets_empty_ts", p);
    }

    /* --- Chrono : master switch + 4 programmes (=switch enable + temp + times) --- */
    char chrono_topic[160], cmd_topic[192];
    snprintf(chrono_topic, sizeof(chrono_topic), "%s/chrono", sub_topic_prefix);

    /* CLEANUP : supprimer TOUTES les anciennes retained discovery chrono qui
     * avaient un composant type ou object_id différent des actuels. */
    {
        char t[192];
        /* Anciens types réutilisés */
        snprintf(t, sizeof(t), "homeassistant/binary_sensor/%s/chrono_master/config", device_id);
        esp_mqtt_client_publish(client, t, "", 0, 1, 1);
        for (int i = 1; i <= 4; i++) {
            /* Anciens obj_id "chrono_prog<N>" (=renommé, plusieurs itérations) */
            snprintf(t, sizeof(t), "homeassistant/sensor/%s/chrono_prog%d/config", device_id, i);
            esp_mqtt_client_publish(client, t, "", 0, 1, 1);
            snprintf(t, sizeof(t), "homeassistant/sensor/%s/chrono_prog%d_sum/config", device_id, i);
            esp_mqtt_client_publish(client, t, "", 0, 1, 1);
            snprintf(t, sizeof(t), "homeassistant/sensor/%s/chrono_prog%d_sum_v2/config", device_id, i);
            esp_mqtt_client_publish(client, t, "", 0, 1, 1);
            snprintf(t, sizeof(t), "homeassistant/text/%s/chrono_prog%d_sum_txt/config", device_id, i);
            esp_mqtt_client_publish(client, t, "", 0, 1, 1);
            /* Anciens text (=maintenant time) */
            snprintf(t, sizeof(t), "homeassistant/text/%s/chrono_prog%d_start/config", device_id, i);
            esp_mqtt_client_publish(client, t, "", 0, 1, 1);
            snprintf(t, sizeof(t), "homeassistant/text/%s/chrono_prog%d_stop/config", device_id, i);
            esp_mqtt_client_publish(client, t, "", 0, 1, 1);
        }
    }

    /* Master switch */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Chrono master");
        cJSON_AddStringToObject(p, "state_topic", chrono_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{{ 'ON' if value_json.master_enabled else 'OFF' }}");
        cJSON_AddStringToObject(p, "payload_on",  "ON");
        cJSON_AddStringToObject(p, "payload_off", "OFF");
        snprintf(cmd_topic, sizeof(cmd_topic), "%s/cmd/chrono_master", sub_topic_prefix);
        cJSON_AddStringToObject(p, "command_topic", cmd_topic);
        cJSON_AddStringToObject(p, "entity_category", "config");
        publish_disco("switch", "chrono_master", p);
    }
    /* Summary sensor per program - reste dans section Contrôles/Capteurs faute
     * de mieux (=sensor + entity_category "config" impossible en HA 2026,
     * text sans command_topic invisible). */
    for (int i = 0; i < 4; i++) {
        cJSON *p = cJSON_CreateObject();
        char name[32], obj[32], tpl[256];
        snprintf(name, sizeof(name), "Chrono P%d résumé", i + 1);
        snprintf(obj,  sizeof(obj),  "chrono_prog%d_sum_v3", i + 1);
        snprintf(tpl, sizeof(tpl),
            "{{ 'actif' if value_json.programs[%d].enabled else 'off' }} "
            "{{ value_json.programs[%d].start }}-{{ value_json.programs[%d].stop }} "
            "{{ value_json.programs[%d].temp_c }}°C", i, i, i, i);
        cJSON_AddStringToObject(p, "name", name);
        cJSON_AddStringToObject(p, "state_topic", chrono_topic);
        cJSON_AddStringToObject(p, "value_template", tpl);
        publish_disco("sensor", obj, p);
    }
    /* Enable switch + temp number + start/stop text per program */
    for (int i = 0; i < 4; i++) {
        int n = i + 1;
        /* Enable switch */
        {
            cJSON *p = cJSON_CreateObject();
            char name[32], obj[32], tpl[128];
            snprintf(name, sizeof(name), "Chrono P%d actif", n);
            snprintf(obj,  sizeof(obj),  "chrono_prog%d_enable", n);
            snprintf(tpl, sizeof(tpl),
                "{{ 'ON' if value_json.programs[%d].enabled else 'OFF' }}", i);
            cJSON_AddStringToObject(p, "name", name);
            cJSON_AddStringToObject(p, "state_topic", chrono_topic);
            cJSON_AddStringToObject(p, "value_template", tpl);
            cJSON_AddStringToObject(p, "payload_on",  "ON");
            cJSON_AddStringToObject(p, "payload_off", "OFF");
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/cmd/chrono_prog%d_enable", sub_topic_prefix, n);
            cJSON_AddStringToObject(p, "command_topic", cmd_topic);
            cJSON_AddStringToObject(p, "entity_category", "config");
            publish_disco("switch", obj, p);
        }
        /* Temp number 1..30°C */
        {
            cJSON *p = cJSON_CreateObject();
            char name[32], obj[32], tpl[128];
            snprintf(name, sizeof(name), "Chrono P%d consigne", n);
            snprintf(obj,  sizeof(obj),  "chrono_prog%d_temp", n);
            snprintf(tpl, sizeof(tpl), "{{ value_json.programs[%d].temp_c }}", i);
            cJSON_AddStringToObject(p, "name", name);
            cJSON_AddStringToObject(p, "state_topic", chrono_topic);
            cJSON_AddStringToObject(p, "value_template", tpl);
            cJSON_AddNumberToObject(p, "min", 1);
            cJSON_AddNumberToObject(p, "max", 30);
            cJSON_AddNumberToObject(p, "step", 1);
            cJSON_AddStringToObject(p, "unit_of_measurement", "°C");
            cJSON_AddStringToObject(p, "mode", "box");
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/cmd/chrono_prog%d_temp", sub_topic_prefix, n);
            cJSON_AddStringToObject(p, "command_topic", cmd_topic);
            cJSON_AddStringToObject(p, "entity_category", "config");
            publish_disco("number", obj, p);
        }
        /* Start + Stop datetime (=HA time picker) */
        for (int se = 0; se < 2; se++) {
            const char *field = se ? "stop" : "start";
            const char *field_fr = se ? "fin" : "début";
            cJSON *p = cJSON_CreateObject();
            char name[32], obj[32], tpl[128];
            snprintf(name, sizeof(name), "Chrono P%d %s", n, field_fr);
            snprintf(obj,  sizeof(obj),  "chrono_prog%d_%s", n, field);
            /* Ajoute ":00" pour format HH:MM:SS attendu par datetime mode time */
            snprintf(tpl, sizeof(tpl), "{{ value_json.programs[%d].%s ~ ':00' }}", i, field);
            cJSON_AddStringToObject(p, "name", name);
            cJSON_AddStringToObject(p, "state_topic", chrono_topic);
            cJSON_AddStringToObject(p, "value_template", tpl);
            cJSON_AddStringToObject(p, "format", "%H:%M:%S");
            snprintf(cmd_topic, sizeof(cmd_topic), "%s/cmd/chrono_prog%d_%s", sub_topic_prefix, n, field);
            cJSON_AddStringToObject(p, "command_topic", cmd_topic);
            cJSON_AddStringToObject(p, "entity_category", "config");
            publish_disco("time", obj, p);
        }
    }

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

    /* --- Binary sensor Safe mode next boot --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Safe mode prochain boot");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{{ 'ON' if value_json.safe_mode_next else 'OFF' }}");
        cJSON_AddStringToObject(p, "payload_on",  "ON");
        cJSON_AddStringToObject(p, "payload_off", "OFF");
        cJSON_AddStringToObject(p, "entity_category", "diagnostic");
        cJSON_AddStringToObject(p, "icon", "mdi:lifebuoy");
        publish_disco("binary_sensor", "safe_mode_next", p);
    }

    /* --- Sensor Uptime (=durée depuis boot en secondes) --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Uptime");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.uptime_s }}");
        cJSON_AddStringToObject(p, "device_class", "duration");
        cJSON_AddStringToObject(p, "unit_of_measurement", "s");
        cJSON_AddStringToObject(p, "state_class", "total_increasing");
        cJSON_AddStringToObject(p, "entity_category", "diagnostic");
        cJSON_AddStringToObject(p, "icon", "mdi:timer-play-outline");
        publish_disco("sensor", "uptime", p);
    }

    /* --- Sensor Last boot (=timestamp dernier démarrage) --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Dernier démarrage");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{{ (now() - timedelta(seconds=value_json.uptime_s | int(0))).isoformat() }}");
        cJSON_AddStringToObject(p, "device_class", "timestamp");
        cJSON_AddStringToObject(p, "entity_category", "diagnostic");
        cJSON_AddStringToObject(p, "icon", "mdi:restart");
        publish_disco("sensor", "last_boot", p);
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

    /* --- 8 binary_sensors alarmes décomposées bit par bit --- */
    {
        static const struct { const char *slug, *fr, *dev_class; } ALARMS[] = {
            {"alarm_sonda_fumi",    "Sonde fumées défectueuse",     "problem"},
            {"alarm_hot_fumi",      "Température fumées trop élevée","heat"},
            {"alarm_fumi_corto",    "Sonde fumées court-circuit",   "problem"},
            {"alarm_aspiratore",    "Aspirateur défectueux",        "problem"},
            {"alarm_no_accensione", "Échec allumage",               "problem"},
            {"alarm_no_fiamma",     "Perte de flamme",              "problem"},
            {"alarm_depression",    "Dépression insuffisante",      "problem"},
            {"alarm_coclea_cmd",    "Alarme commande vis sans fin", "problem"},
        };
        for (size_t i = 0; i < sizeof(ALARMS)/sizeof(ALARMS[0]); i++) {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "name", ALARMS[i].fr);
            cJSON_AddStringToObject(p, "state_topic", state_topic);
            char tmpl[128];
            snprintf(tmpl, sizeof(tmpl), "{{ 'ON' if value_json.%s else 'OFF' }}", ALARMS[i].slug);
            cJSON_AddStringToObject(p, "value_template", tmpl);
            cJSON_AddStringToObject(p, "payload_on",  "ON");
            cJSON_AddStringToObject(p, "payload_off", "OFF");
            cJSON_AddStringToObject(p, "device_class", ALARMS[i].dev_class);
            publish_disco("binary_sensor", ALARMS[i].slug, p);
        }
    }

    /* --- Sensors maintenance : heures/allumages avant service/nettoyage --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Heures avant service");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.hours_before_service }}");
        cJSON_AddStringToObject(p, "unit_of_measurement", "h");
        cJSON_AddStringToObject(p, "icon", "mdi:wrench-clock");
        publish_disco("sensor", "hours_before_service", p);
    }
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Heures depuis service");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.hours_since_service }}");
        cJSON_AddStringToObject(p, "unit_of_measurement", "h");
        cJSON_AddStringToObject(p, "state_class", "total_increasing");
        cJSON_AddStringToObject(p, "entity_category", "diagnostic");
        publish_disco("sensor", "hours_since_service", p);
    }
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Allumages avant nettoyage");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.starts_before_cleaning }}");
        cJSON_AddStringToObject(p, "icon", "mdi:fire-alert");
        publish_disco("sensor", "starts_before_cleaning", p);
    }
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Allumages depuis nettoyage");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.starts_since_cleaning }}");
        cJSON_AddStringToObject(p, "state_class", "total_increasing");
        cJSON_AddStringToObject(p, "entity_category", "diagnostic");
        publish_disco("sensor", "starts_since_cleaning", p);
    }

    /* --- Sensor Historique alarmes : count en state, events en attribute --- */
    {
        char h_topic[160];
        snprintf(h_topic, sizeof(h_topic), "%s/history_alarms", sub_topic_prefix);
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Historique alarmes");
        cJSON_AddStringToObject(p, "state_topic", h_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.count }}");
        cJSON_AddStringToObject(p, "json_attributes_topic", h_topic);
        cJSON_AddStringToObject(p, "json_attributes_template",
            "{{ {'events': value_json.events} | tojson }}");
        cJSON_AddStringToObject(p, "icon", "mdi:history");
        cJSON_AddStringToObject(p, "entity_category", "diagnostic");
        publish_disco("sensor", "history_alarms", p);
    }

    /* --- Sensor Params tech UT04 : state=div count, attrs=table Pr01-Pr30 --- */
    {
        char t_topic[160];
        snprintf(t_topic, sizeof(t_topic), "%s/params_tech", sub_topic_prefix);
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Params tech divergents");
        cJSON_AddStringToObject(p, "state_topic", t_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.divergent_count }}");
        cJSON_AddStringToObject(p, "json_attributes_topic", t_topic);
        cJSON_AddStringToObject(p, "icon", "mdi:tune-vertical");
        cJSON_AddStringToObject(p, "entity_category", "diagnostic");
        publish_disco("sensor", "params_tech", p);
    }

    /* --- Sensor Diagnostic combustion : state=severity, attrs=liste diagnostics --- */
    {
        char d_topic[160];
        snprintf(d_topic, sizeof(d_topic), "%s/combustion_diag", sub_topic_prefix);
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Diagnostic combustion");
        cJSON_AddStringToObject(p, "state_topic", d_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.severity }}");
        cJSON_AddStringToObject(p, "json_attributes_topic", d_topic);
        cJSON_AddStringToObject(p, "icon", "mdi:fire-alert");
        cJSON_AddStringToObject(p, "entity_category", "diagnostic");
        publish_disco("sensor", "combustion_diag", p);
    }

    /* --- Binary sensor Trémie vide (=alerte pellets épuisés) --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Trémie vide");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{{ 'ON' if value_json.tremie_vide else 'OFF' }}");
        cJSON_AddStringToObject(p, "payload_on",  "ON");
        cJSON_AddStringToObject(p, "payload_off", "OFF");
        cJSON_AddStringToObject(p, "device_class", "problem");
        cJSON_AddStringToObject(p, "icon", "mdi:tank");
        publish_disco("binary_sensor", "tremie_vide", p);
    }

    /* --- Sensor modulation --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Modulation");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template", "{{ value_json.modulation }}");
        cJSON_AddStringToObject(p, "unit_of_measurement", "%");
        cJSON_AddStringToObject(p, "icon", "mdi:sine-wave");
        publish_disco("sensor", "modulation", p);
    }

    /* --- 2 buttons HA : reset service + reset cleaning --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Reset compteur service");
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "%s/cmd/reset_service", sub_topic_prefix);
        cJSON_AddStringToObject(p, "command_topic", cmd);
        cJSON_AddStringToObject(p, "payload_press", "1");
        cJSON_AddStringToObject(p, "entity_category", "config");
        cJSON_AddStringToObject(p, "icon", "mdi:restart-alert");
        publish_disco("button", "reset_service", p);
    }
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Reset compteur nettoyage");
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "%s/cmd/reset_cleaning", sub_topic_prefix);
        cJSON_AddStringToObject(p, "command_topic", cmd);
        cJSON_AddStringToObject(p, "payload_press", "1");
        cJSON_AddStringToObject(p, "entity_category", "config");
        cJSON_AddStringToObject(p, "icon", "mdi:broom");
        publish_disco("button", "reset_cleaning", p);
    }
    /* --- Rollback firmware (=recovery vers ota_1 précédent) --- */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Rollback firmware");
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "%s/cmd/rollback_firmware", sub_topic_prefix);
        cJSON_AddStringToObject(p, "command_topic", cmd);
        cJSON_AddStringToObject(p, "payload_press", "1");
        cJSON_AddStringToObject(p, "entity_category", "diagnostic");
        cJSON_AddStringToObject(p, "icon", "mdi:undo-variant");
        publish_disco("button", "rollback_firmware", p);
    }

#ifdef TARGET_BLACKLABEL
    /* Switch Cloud on/off publié uniquement si TC2 login/mdp configurés
     * (=sinon ON n'a aucun effet, on cache le switch pour éviter confusion).
     * Le user configure d'abord dans le web UI, PUIS le switch apparaît HA. */
    if (local_cfg.tc2_username[0] && local_cfg.tc2_password[0]) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Cloud Extraflame");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{{ 'ON' if value_json.cloud_enabled else 'OFF' }}");
        char cmd_cloud[160];
        snprintf(cmd_cloud, sizeof(cmd_cloud), "%s/cmd/cloud_toggle", sub_topic_prefix);
        cJSON_AddStringToObject(p, "command_topic", cmd_cloud);
        cJSON_AddStringToObject(p, "payload_on",  "1");
        cJSON_AddStringToObject(p, "payload_off", "0");
        cJSON_AddStringToObject(p, "state_on",  "ON");
        cJSON_AddStringToObject(p, "state_off", "OFF");
        /* Pas de entity_category = apparaît dans les contrôles principaux
         * de la carte poêle HA (=pas dans Configuration cachée). */
        cJSON_AddStringToObject(p, "icon", "mdi:cloud");
        publish_disco("switch", "cloud", p);
    } else {
        /* Nettoie retained si l'user vient de vider les creds via UI. */
        char t[192];
        snprintf(t, sizeof(t), "homeassistant/switch/%s/cloud/config", device_id);
        esp_mqtt_client_publish(client, t, "", 0, 0, 1);
    }

    /* Binary sensor Cloud connecté (=état live info, toujours publié) */
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "name", "Cloud Extraflame connecté");
        cJSON_AddStringToObject(p, "state_topic", state_topic);
        cJSON_AddStringToObject(p, "value_template",
            "{{ 'ON' if value_json.cloud_connected else 'OFF' }}");
        cJSON_AddStringToObject(p, "payload_on",  "ON");
        cJSON_AddStringToObject(p, "payload_off", "OFF");
        cJSON_AddStringToObject(p, "device_class", "connectivity");
        cJSON_AddStringToObject(p, "entity_category", "diagnostic");
        publish_disco("binary_sensor", "cloud_connected", p);
    }

    /* Nettoie le vieux binary_sensor cloud_enabled (=retained legacy) */
    {
        char t[192];
        snprintf(t, sizeof(t), "homeassistant/binary_sensor/%s/cloud_enabled/config", device_id);
        esp_mqtt_client_publish(client, t, "", 0, 0, 1);
    }
#endif

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
