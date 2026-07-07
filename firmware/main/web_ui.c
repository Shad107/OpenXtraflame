/**
 * openextraflame - HTTP server
 *
 * Endpoints:
 *  GET  /            -> index.html
 *  GET  /style.css   -> style.css
 *  GET  /script.js   -> script.js
 *  GET  /ap.json     -> Wi-Fi scan results
 *  GET  /status.json -> Wi-Fi + MQTT + stove status
 *  GET  /config.json -> current config
 *  POST /config.json -> update config
 *  POST /connect.json -> connect to Wi-Fi (=triggers reboot to STA)
 *  POST /reboot      -> reboot module
 *  POST /factory     -> factory reset NVS
 */

#include <string.h>
#include <stdlib.h>
#include <sys/param.h>   /* for MIN() macro */
#include "web_ui.h"
#include "wifi_bridge.h"
#include "micronova.h"
#include "ota.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "esp_wifi.h"
#include "cJSON.h"

extern const char index_html_start[]  asm("_binary_index_html_start");
extern const char index_html_end[]    asm("_binary_index_html_end");
extern const char style_css_start[]   asm("_binary_style_css_start");
extern const char style_css_end[]     asm("_binary_style_css_end");
extern const char script_js_start[]   asm("_binary_script_js_start");
extern const char script_js_end[]     asm("_binary_script_js_end");

static const char *TAG = "WEB";
static app_config_t *g_cfg = NULL;

/* Force browsers to re-fetch on every load so an OTA that touches
 * index.html / style.css / script.js is picked up on next open. */
#define SET_NO_CACHE(req) httpd_resp_set_hdr(req, "Cache-Control", "no-store, must-revalidate")

/* EMBED_TXTFILES appends a NUL terminator to the blob (=documented behavior).
 * `end - start` therefore includes the NUL. Strip it before sending or strict
 * parsers (=some mobile browsers, some JS engines) reject the trailing U+0000
 * with a SyntaxError and refuse to execute the whole script. Beta3 was
 * shipping the NUL, which is what showed up as a stray 'NUL' at the end of
 * script.js in the user's browser console. */
static esp_err_t handle_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    SET_NO_CACHE(req);
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t handle_css(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    SET_NO_CACHE(req);
    return httpd_resp_send(req, style_css_start, style_css_end - style_css_start - 1);
}

static esp_err_t handle_js(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    SET_NO_CACHE(req);
    return httpd_resp_send(req, script_js_start, script_js_end - script_js_start - 1);
}

static esp_err_t handle_ap_json(httpd_req_t *req)
{
    char *json = wifi_bridge_scan_json();
    httpd_resp_set_type(req, "application/json");
    if (!json) {                              /* scan/alloc failed -> don't strlen(NULL) */
        return httpd_resp_sendstr(req, "[]");
    }
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static esp_err_t handle_stove_cmd(httpd_req_t *req)
{
    /* Extract command from URL after /api/stove/ */
    const char *cmd = req->uri + strlen("/api/stove/");
    if (strcmp(cmd, "on") == 0)          mn_set_ram(MN_RAM_ACCENDI, 1);
    else if (strcmp(cmd, "off") == 0)    mn_set_ram(MN_RAM_SPEGNI, 1);
    else if (strcmp(cmd, "reset_alarm") == 0) mn_set_ram(MN_RAM_SBLOCCO, 1);
    else return httpd_resp_send_404(req);
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

extern EventGroupHandle_t app_event_group;
extern const int MQTT_CONNECTED_BIT;

static esp_err_t handle_status_json(httpd_req_t *req)
{
    mn_stove_state_snapshot_t s;
    mn_get_snapshot(&s);

    cJSON *o = cJSON_CreateObject();
    char *wifi_json = wifi_bridge_status_json();
    cJSON *wifi = cJSON_Parse(wifi_json);
    cJSON_AddItemToObject(o, "wifi", wifi);
    free(wifi_json);

    cJSON *mqtt = cJSON_CreateObject();
    EventBits_t bits = xEventGroupGetBits(app_event_group);
    cJSON_AddBoolToObject(mqtt, "connected", (bits & MQTT_CONNECTED_BIT) != 0);
    cJSON_AddItemToObject(o, "mqtt", mqtt);

    cJSON *stove = cJSON_CreateObject();
    cJSON_AddBoolToObject(stove, "online", s.online);
    cJSON_AddNumberToObject(stove, "state", s.state);
    cJSON_AddNumberToObject(stove, "power", s.power_level);
    cJSON_AddNumberToObject(stove, "t_ambient", s.t_ambient);
    cJSON_AddNumberToObject(stove, "t_smoke", s.t_smoke);
    cJSON_AddItemToObject(o, "stove", stove);

    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));
    free(out);
    return ESP_OK;
}

static esp_err_t handle_config_get(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o,   "provisioned",  g_cfg->provisioned);
    cJSON_AddStringToObject(o, "wifi_ssid",    g_cfg->wifi_ssid);
    /* Never echo the passwords themselves, but flag whether they exist so
     * the UI can render 'already set, leave blank to keep' instead of an
     * empty field the user thinks he must retype. */
    cJSON_AddBoolToObject(o,   "wifi_password_set", g_cfg->wifi_password[0] != '\0');
    cJSON_AddBoolToObject(o,   "mqtt_password_set", g_cfg->mqtt_password[0] != '\0');
    cJSON_AddStringToObject(o, "mqtt_host",    g_cfg->mqtt_host);
    cJSON_AddNumberToObject(o, "mqtt_port",    g_cfg->mqtt_port);
    cJSON_AddStringToObject(o, "mqtt_user",    g_cfg->mqtt_username);
    cJSON_AddStringToObject(o, "mqtt_prefix",  g_cfg->mqtt_topic_prefix);
    cJSON_AddBoolToObject(o,   "mqtt_tls",     g_cfg->mqtt_use_tls);
    cJSON_AddNumberToObject(o, "stove_type",   g_cfg->stove_type);
    cJSON_AddStringToObject(o, "stove_name",   g_cfg->stove_name);
    cJSON_AddBoolToObject(o,   "ha_discovery", g_cfg->ha_discovery_enabled);
    cJSON_AddNumberToObject(o, "publish_interval_ms", g_cfg->publish_interval_ms);

    /* Guardian mode (=only meaningful on Blacklabel target) */
#ifdef TARGET_BLACKLABEL
    cJSON_AddBoolToObject(o,   "guardian_supported", true);
    cJSON_AddBoolToObject(o,   "guardian_enabled",   g_cfg->guardian_enabled);
    cJSON_AddStringToObject(o, "guardian_url",       g_cfg->guardian_archive_url);
    cJSON_AddNumberToObject(o, "guardian_action",    g_cfg->guardian_action_mode);
#else
    cJSON_AddBoolToObject(o,   "guardian_supported", false);
#endif

    /* Firmware version from app descriptor */
    const esp_app_desc_t *desc = esp_app_get_description();
    cJSON_AddStringToObject(o, "version", desc ? desc->version : "unknown");
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));
    free(out);
    return ESP_OK;
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t buf_len)
{
    int total = 0;
    int remaining = req->content_len;
    while (remaining > 0 && total < (int)buf_len - 1) {
        int r = httpd_req_recv(req, buf + total,
                                MIN(remaining, (int)(buf_len - 1 - total)));
        if (r <= 0) return ESP_FAIL;
        total += r;
        remaining -= r;
    }
    buf[total] = '\0';
    return ESP_OK;
}

static esp_err_t handle_config_post(httpd_req_t *req)
{
    char buf[1024];
    if (read_body(req, buf, sizeof(buf)) != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    cJSON *o = cJSON_Parse(buf);
    if (!o) return httpd_resp_send_500(req);

#define GET_STR(k, dst) do { \
    cJSON *v = cJSON_GetObjectItem(o, k); \
    if (cJSON_IsString(v)) strncpy(dst, v->valuestring, sizeof(dst) - 1); \
} while(0)
/* Password fields: browsers do not autofill password inputs on POST
 * so a form submitted from any other tab (=Stove, MQTT, Advanced)
 * sends an empty string, which used to overwrite the stored Wi-Fi
 * / MQTT credentials. Skip empty strings for password fields. */
#define GET_STR_KEEP_EMPTY(k, dst) do { \
    cJSON *v = cJSON_GetObjectItem(o, k); \
    if (cJSON_IsString(v) && v->valuestring[0] != '\0') \
        strncpy(dst, v->valuestring, sizeof(dst) - 1); \
} while(0)
#define GET_NUM(k, dst, cast) do { \
    cJSON *v = cJSON_GetObjectItem(o, k); \
    if (cJSON_IsNumber(v)) dst = (cast)v->valuedouble; \
} while(0)
#define GET_BOOL(k, dst) do { \
    cJSON *v = cJSON_GetObjectItem(o, k); \
    if (cJSON_IsBool(v)) dst = cJSON_IsTrue(v); \
} while(0)

    /* Identity fields (=SSID, broker, topic prefix, friendly name) use
     * KEEP_EMPTY too: a save from any tab must never wipe them by
     * accident. Users who genuinely want to clear a field can use the
     * Factory Reset button. mqtt_user stays with GET_STR because empty
     * is a legitimate value (=broker without auth). */
    GET_STR_KEEP_EMPTY("wifi_ssid",    g_cfg->wifi_ssid);
    GET_STR_KEEP_EMPTY("wifi_pwd",     g_cfg->wifi_password);
    GET_STR_KEEP_EMPTY("mqtt_host",    g_cfg->mqtt_host);
    GET_NUM("mqtt_port",               g_cfg->mqtt_port, uint16_t);
    GET_STR("mqtt_user",               g_cfg->mqtt_username);
    GET_STR_KEEP_EMPTY("mqtt_pwd",     g_cfg->mqtt_password);
    GET_STR_KEEP_EMPTY("mqtt_prefix",  g_cfg->mqtt_topic_prefix);
    GET_BOOL("mqtt_tls",               g_cfg->mqtt_use_tls);
    GET_NUM("stove_type",              g_cfg->stove_type, stove_type_t);
    GET_STR_KEEP_EMPTY("stove_name",   g_cfg->stove_name);
    GET_BOOL("ha_discovery", g_cfg->ha_discovery_enabled);
    GET_NUM("publish_interval_ms", g_cfg->publish_interval_ms, uint16_t);

    /* Provisioning done as soon as the user gave us a Wi-Fi SSID: MQTT can
     * be configured later from the same UI once the module is in STA mode.
     * Previously we required mqtt_host too, which trapped users who only
     * filled the Wi-Fi form and rebooted (=module fell back to SoftAP). */
    if (strlen(g_cfg->wifi_ssid) > 0) {
        g_cfg->provisioned = true;
    }

    cJSON_Delete(o);
    config_nvs_save(g_cfg);
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t handle_reboot(httpd_req_t *req)
{
    httpd_resp_send(req, "{\"ok\":true}", 11);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t handle_factory(httpd_req_t *req)
{
    config_nvs_reset();
    httpd_resp_send(req, "{\"ok\":true,\"rebooting\":true}", 27);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* POST /ota/upload : streamed firmware binary -> inactive OTA slot, then reboot */
static esp_err_t handle_ota_upload(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }
    if (ota_upload_begin(total) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin failed");
    }

    /* Wi-Fi modem sleep would freeze the RX ring while the flash is being
     * written on ESP32 v1.0 => browser sees the connection go silent and
     * aborts. Force full-power for the OTA and restore afterwards. */
    wifi_ps_type_t saved_ps = WIFI_PS_MIN_MODEM;
    esp_wifi_get_ps(&saved_ps);
    esp_wifi_set_ps(WIFI_PS_NONE);

    /* Buffer on the heap, not on the stack: the httpd task's 4096-byte
     * stack is not enough to hold 1460 bytes + the SHA256 verification
     * context that esp_ota_end() will consume at the end of the upload
     * (=beta15 stack overflowed here). */
    char *buf = malloc(1460);
    if (!buf) {
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        ota_upload_abort();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "malloc failed");
    }
    int remaining = total;
    int chunk_count = 0;
    esp_err_t result = ESP_OK;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, MIN(remaining, 1460));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            /* Wi-Fi hiccup or browser pause; keep the OTA alive and retry
             * instead of failing the whole upload. Common on ESP32 v1.0
             * when a burst of flash writes stalls the RX task for >5s. */
            continue;
        }
        if (r <= 0) {
            result = ESP_FAIL;
            break;
        }
        if (ota_upload_data(buf, r) != ESP_OK) {
            result = ESP_FAIL;
            break;
        }
        remaining -= r;
        /* Yield to Wi-Fi driver every ~16 chunks (=~24KB) instead of every
         * chunk. Every-chunk yield added ~6s to a 900KB upload for no gain;
         * every 16 chunks is enough to let beacons + acks pass through. */
        if ((++chunk_count & 0x0F) == 0) {
            vTaskDelay(1);
        }
    }

    /* Restore original power-save mode before we return either way. */
    esp_wifi_set_ps(saved_ps);
    free(buf);

    if (result != ESP_OK) {
        ota_upload_abort();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload failed");
    }
    /* Reply BEFORE finalizing: ota_upload_end() verifies + reboots on success. */
    httpd_resp_send(req, "{\"ok\":true}", 11);
    ota_upload_end();
    return ESP_OK;
}

/* POST /ota/rollback : boot the previous OTA slot and reboot */
static esp_err_t handle_ota_rollback(httpd_req_t *req)
{
    httpd_resp_send(req, "{\"ok\":true}", 11);
    vTaskDelay(pdMS_TO_TICKS(300));
    ota_rollback();
    return ESP_OK;
}

/* GET /ota/status : current OTA progress/state as JSON */
static esp_err_t handle_ota_status(httpd_req_t *req)
{
    ota_status_t st;
    ota_get_status(&st);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "state",   st.state);
    cJSON_AddNumberToObject(o, "total",   st.total_bytes);
    cJSON_AddNumberToObject(o, "written", st.written_bytes);
    cJSON_AddStringToObject(o, "message", st.message);
    cJSON_AddStringToObject(o, "version", st.active_version);
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));
    free(out);
    return ESP_OK;
}

/* --- Debug : dump Micronova ring buffer to the browser --- */

static esp_err_t handle_debug_uart(httpd_req_t *req)
{
    char *json = mn_debug_dump_json();
    if (!json) return httpd_resp_sendstr(req, "{\"frames\":[]}");
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* --- Pull an OTA image from an arbitrary URL (github release, mirror, etc.) --- */

static void ota_pull_bg_task(void *arg)
{
    char *url = (char *)arg;
    esp_err_t err = ota_pull_from_url(url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_pull_from_url(%s) failed: %s", url, esp_err_to_name(err));
    }
    free(url);
    vTaskDelete(NULL);
}

static esp_err_t handle_ota_pull(httpd_req_t *req)
{
    char buf[512];
    if (read_body(req, buf, sizeof(buf)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "read body");
    }
    cJSON *o = cJSON_Parse(buf);
    cJSON *u = o ? cJSON_GetObjectItem(o, "url") : NULL;
    if (!cJSON_IsString(u) || strlen(u->valuestring) < 8) {
        if (o) cJSON_Delete(o);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "url required");
    }
    char *url_copy = strdup(u->valuestring);
    cJSON_Delete(o);
    if (!url_copy) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    if (xTaskCreate(ota_pull_bg_task, "ota_pull", 8192, url_copy, 5, NULL) != pdPASS) {
        free(url_copy);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "task fail");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

esp_err_t web_ui_start(app_config_t *cfg)
{
    g_cfg = cfg;

    httpd_config_t hd = HTTPD_DEFAULT_CONFIG();
    hd.max_uri_handlers   = 20;   /* we currently register 18 routes; the default 8
                                   * silently dropped /ota/pull and /debug/uart. */
    hd.stack_size         = 6144; /* default 4096 stack-overflows during esp_ota_end()
                                   * SHA256 verify at the end of an OTA upload (=beta15
                                   * dump). 6144 leaves headroom without triggering the
                                   * LwIP RST issue that a full 8192 bump did. */
    /* Everything else stays at the ESP-IDF default. Earlier revisions bumped
     * max_open_sockets, recv/send_wait_timeout and set lru_purge_enable=true.
     * On Black Label hardware those combinations produced a silent LwIP RST
     * on the first browser GET with zero httpd_* log line, so we still keep
     * them at the default. Only the stack_size bump was necessary. */
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &hd));

    httpd_uri_t routes[] = {
        { .uri = "/",                     .method = HTTP_GET,  .handler = handle_index,        },
        { .uri = "/style.css",            .method = HTTP_GET,  .handler = handle_css,          },
        { .uri = "/script.js",            .method = HTTP_GET,  .handler = handle_js,           },
        { .uri = "/ap.json",              .method = HTTP_GET,  .handler = handle_ap_json,      },
        { .uri = "/status.json",          .method = HTTP_GET,  .handler = handle_status_json,  },
        { .uri = "/config.json",          .method = HTTP_GET,  .handler = handle_config_get,   },
        { .uri = "/config.json",          .method = HTTP_POST, .handler = handle_config_post,  },
        { .uri = "/reboot",               .method = HTTP_POST, .handler = handle_reboot,       },
        { .uri = "/factory",              .method = HTTP_POST, .handler = handle_factory,      },
        /* stove commands (were defined but not registered -> 404) */
        { .uri = "/api/stove/on",         .method = HTTP_POST, .handler = handle_stove_cmd,    },
        { .uri = "/api/stove/off",        .method = HTTP_POST, .handler = handle_stove_cmd,    },
        { .uri = "/api/stove/reset_alarm",.method = HTTP_POST, .handler = handle_stove_cmd,    },
        /* OTA */
        { .uri = "/ota/upload",           .method = HTTP_POST, .handler = handle_ota_upload,   },
        { .uri = "/ota/rollback",         .method = HTTP_POST, .handler = handle_ota_rollback, },
        { .uri = "/ota/pull",             .method = HTTP_POST, .handler = handle_ota_pull,     },
        { .uri = "/debug/uart",           .method = HTTP_GET,  .handler = handle_debug_uart,   },
        { .uri = "/ota/status",           .method = HTTP_GET,  .handler = handle_ota_status,   },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "Web UI listening on :80");
    return ESP_OK;
}
