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
#include "mqtt_bridge.h"
#include "micronova.h"
#include "ota.h"
#include "log_ring.h"
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
    if (strcmp(cmd, "on") == 0)          mn_write_register(MN_RAM_ACCENDI, 1);
    else if (strcmp(cmd, "off") == 0)    mn_write_register(MN_RAM_SPEGNI, 1);
    else if (strcmp(cmd, "reset_alarm") == 0) mn_write_register(MN_RAM_SBLOCCO, 1);
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
    cJSON_AddBoolToObject(o,   "mqtt_user_set",     g_cfg->mqtt_username[0] != '\0');
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
    if (cJSON_IsString(v)) { \
        strncpy(dst, v->valuestring, sizeof(dst) - 1); \
        dst[sizeof(dst) - 1] = '\0';  /* strncpy leaves no \0 if src full-width */ \
    } \
} while(0)
/* Password fields: browsers do not autofill password inputs on POST
 * so a form submitted from any other tab (=Stove, MQTT, Advanced)
 * sends an empty string, which used to overwrite the stored Wi-Fi
 * / MQTT credentials. Skip empty strings for password fields. */
#define GET_STR_KEEP_EMPTY(k, dst) do { \
    cJSON *v = cJSON_GetObjectItem(o, k); \
    if (cJSON_IsString(v) && v->valuestring[0] != '\0') { \
        strncpy(dst, v->valuestring, sizeof(dst) - 1); \
        dst[sizeof(dst) - 1] = '\0'; \
    } \
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
    GET_STR_KEEP_EMPTY("mqtt_user",    g_cfg->mqtt_username);
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

/* GET /mqtt/debug : quick introspection of what's actually stored in NVS
 * for MQTT credentials. Returns the username in clear + lengths for the
 * password fields (=never the value). Lets users diagnose 'save didn't
 * really persist my mdp' without having to reboot + read miniterm.  */
static esp_err_t handle_mqtt_debug(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "host",       g_cfg->mqtt_host);
    cJSON_AddNumberToObject(o, "port",       g_cfg->mqtt_port);
    cJSON_AddStringToObject(o, "user",       g_cfg->mqtt_username);
    cJSON_AddNumberToObject(o, "user_len",   (int)strlen(g_cfg->mqtt_username));
    cJSON_AddNumberToObject(o, "pwd_len",    (int)strlen(g_cfg->mqtt_password));
    cJSON_AddStringToObject(o, "prefix",     g_cfg->mqtt_topic_prefix);
    cJSON_AddBoolToObject(o,   "tls",        g_cfg->mqtt_use_tls);
    /* Live connection state (=MQTT_CONNECTED_BIT flag maintained by
     * mqtt_bridge event handler). */
    extern EventGroupHandle_t app_event_group;
    extern const int MQTT_CONNECTED_BIT;
    EventBits_t bits = xEventGroupGetBits(app_event_group);
    cJSON_AddBoolToObject(o, "connected", (bits & MQTT_CONNECTED_BIT) != 0);

    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, out, strlen(out));
    free(out);
    return ESP_OK;
}

/* POST /mqtt/test : quick broker connectivity test with the params in the
 * JSON body. Empty user/pwd -> anonymous or use stored (=if provided as
 * empty via UI intent). Returns {ok, message}. */
static esp_err_t handle_mqtt_test(httpd_req_t *req)
{
    char buf[512];
    if (read_body(req, buf, sizeof(buf)) != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    cJSON *o = cJSON_Parse(buf);
    if (!o) return httpd_resp_send_500(req);

    const cJSON *h  = cJSON_GetObjectItem(o, "host");
    const cJSON *p  = cJSON_GetObjectItem(o, "port");
    const cJSON *u  = cJSON_GetObjectItem(o, "user");
    const cJSON *w  = cJSON_GetObjectItem(o, "pwd");
    const cJSON *tls = cJSON_GetObjectItem(o, "tls");

    const char *host = (cJSON_IsString(h) && h->valuestring[0]) ? h->valuestring : NULL;
    int         port = cJSON_IsNumber(p) ? (int)p->valuedouble : 1883;
    const char *user = (cJSON_IsString(u) && u->valuestring[0]) ? u->valuestring : g_cfg->mqtt_username;
    /* Empty password from UI = use the stored one (=lets user test without retyping). */
    const char *pwd  = (cJSON_IsString(w) && w->valuestring[0]) ? w->valuestring : g_cfg->mqtt_password;
    bool use_tls     = cJSON_IsBool(tls) ? cJSON_IsTrue(tls) : g_cfg->mqtt_use_tls;

    if (!host) {
        cJSON_Delete(o);
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "ok", false);
        cJSON_AddStringToObject(r, "message", "Host manquant");
        char *out = cJSON_PrintUnformatted(r);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, out, strlen(out));
        free(out); cJSON_Delete(r);
        return ESP_OK;
    }

    char msg[96] = {0};
    esp_err_t err = mqtt_bridge_test(host, (uint16_t)port, user, pwd, use_tls,
                                      msg, sizeof(msg));
    cJSON_Delete(o);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", err == ESP_OK);
    cJSON_AddStringToObject(r, "message", msg);
    char *out = cJSON_PrintUnformatted(r);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));
    free(out); cJSON_Delete(r);
    return ESP_OK;
}

/* GET /mqtt/discover : do a live mDNS lookup for _mqtt._tcp and return
 * {"host":"192.168.50.7","port":1883} or {"host":""} on timeout. */
static esp_err_t handle_mqtt_discover(httpd_req_t *req)
{
    char found[48] = "";
    wifi_bridge_mdns_query_mqtt(found, sizeof(found));

    cJSON *o = cJSON_CreateObject();
    if (found[0] != '\0') {
        char *colon = strchr(found, ':');
        if (colon) {
            *colon = '\0';
            cJSON_AddStringToObject(o, "host", found);
            cJSON_AddNumberToObject(o, "port", atoi(colon + 1));
        } else {
            cJSON_AddStringToObject(o, "host", found);
            cJSON_AddNumberToObject(o, "port", 1883);
        }
    } else {
        cJSON_AddStringToObject(o, "host", "");
        cJSON_AddStringToObject(o, "message", "Aucun broker MQTT trouvé sur le LAN (=mDNS timeout)");
    }
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));
    free(out);
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

/* GET /debug/ram : dump the Micronova RAM shadow (=all known registers
 * with their current shadow value) so users can audit exactly what the
 * module thinks the stove reported / what will be replayed to it. */
static esp_err_t handle_debug_ram(httpd_req_t *req)
{
    char *json = mn_ram_dump_json();
    if (!json) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* GET /api/registers : live register table with names and decoded hints. */
static esp_err_t handle_registers_live(httpd_req_t *req)
{
    char *json = mn_registers_live_json();
    if (!json) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* GET/POST /debug/poll-list :
 *   GET  → current poll list (=list of uint16 addresses).
 *   POST body {"list":[209,208,...]} → replace poll list. */
static esp_err_t handle_poll_list(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        char body[512] = {0};
        int rd = httpd_req_recv(req, body, sizeof(body) - 1);
        if (rd <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        cJSON *root = cJSON_Parse(body);
        if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        cJSON *list = cJSON_GetObjectItem(root, "list");
        if (!cJSON_IsArray(list)) { cJSON_Delete(root); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no list array"); }
        int n = cJSON_GetArraySize(list);
        uint16_t addrs[32]; int nn = 0;
        for (int i = 0; i < n && nn < 32; i++) {
            cJSON *e = cJSON_GetArrayItem(list, i);
            if (cJSON_IsNumber(e)) addrs[nn++] = (uint16_t)e->valueint;
        }
        cJSON_Delete(root);
        esp_err_t err = mn_poll_list_set(addrs, nn);
        if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "set failed");
    }
    char *json = mn_poll_list_get_json();
    if (!json) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* GET/POST /debug/poll-interval :
 *   GET → {"ms":N}
 *   POST body {"ms":N} → set (=20..5000). */
static esp_err_t handle_poll_interval(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        char body[64] = {0};
        int rd = httpd_req_recv(req, body, sizeof(body) - 1);
        if (rd <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        cJSON *root = cJSON_Parse(body);
        if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        cJSON *ms = cJSON_GetObjectItem(root, "ms");
        int m = cJSON_IsNumber(ms) ? ms->valueint : 0;
        cJSON_Delete(root);
        if (mn_poll_interval_set(m) != ESP_OK)
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid ms");
    }
    char resp[48];
    snprintf(resp, sizeof(resp), "{\"ms\":%d}", mn_poll_interval_get());
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    return httpd_resp_send(req, resp, strlen(resp));
}

/* GET /debug/mnstats : Micronova UART runtime stats for audit. */
static esp_err_t handle_debug_mnstats(httpd_req_t *req)
{
    char *json = mn_stats_json();
    if (!json) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* GET /debug/logs : dump the ESP_LOG ring buffer as a plain text stream. */
static esp_err_t handle_debug_logs(httpd_req_t *req)
{
    char *dump = log_ring_dump();
    if (!dump) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    SET_NO_CACHE(req);
    httpd_resp_send(req, dump, strlen(dump));
    free(dump);
    return ESP_OK;
}

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

/* POST /debug/tx-check - verify our TX pin actually pulses by sampling
 * GPIO23 as input while UART sends a burst. */
static esp_err_t handle_debug_tx_check(httpd_req_t *req)
{
    char *out = mn_tx_self_check();
    if (!out) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, out, strlen(out));
    free(out);
    return ESP_OK;
}

/* POST /debug/rx-raw?ms=500 - bypass UART, sample GPIO5 raw at ~100kHz. */
static esp_err_t handle_debug_rx_raw(httpd_req_t *req)
{
    int ms = 500;
    char q[32];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char v[16];
        if (httpd_query_key_value(q, "ms", v, sizeof(v)) == ESP_OK) ms = atoi(v);
    }
    char *out = mn_rx_raw_sample(ms);
    if (!out) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, out, strlen(out));
    free(out);
    return ESP_OK;
}

/* POST /debug/uart-set  body: {"baud":38400,"rx_inv":true,"tx_inv":false}
 * Live UART reconfig without reboot. */
static esp_err_t handle_debug_uart_set(httpd_req_t *req)
{
    char body[128] = {0};
    int rd = httpd_req_recv(req, body, sizeof(body) - 1);
    if (rd <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    int baud   = 0, rx_inv = -1, tx_inv = -1, stop_bits = 0;
    cJSON *j;
    j = cJSON_GetObjectItem(root, "baud");      if (cJSON_IsNumber(j)) baud      = j->valueint;
    j = cJSON_GetObjectItem(root, "rx_inv");    if (cJSON_IsBool(j))   rx_inv    = cJSON_IsTrue(j) ? 1 : 0;
    j = cJSON_GetObjectItem(root, "tx_inv");    if (cJSON_IsBool(j))   tx_inv    = cJSON_IsTrue(j) ? 1 : 0;
    j = cJSON_GetObjectItem(root, "stop_bits"); if (cJSON_IsNumber(j)) stop_bits = j->valueint;
    cJSON_Delete(root);

    char *out = mn_reconfig_uart_ex(baud, rx_inv, tx_inv, stop_bits);
    if (!out) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, out, strlen(out));
    free(out);
    return ESP_OK;
}

/* POST /debug/uart-tx  body: {"hex":"20 00","timeout_ms":500}
 * Fires arbitrary bytes on UART1 and returns whatever comes back. */
static esp_err_t handle_debug_uart_tx(httpd_req_t *req)
{
    char body[256] = {0};
    int rd = httpd_req_recv(req, body, sizeof(body) - 1);
    if (rd <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
    cJSON *root = cJSON_Parse(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    const cJSON *hex  = cJSON_GetObjectItem(root, "hex");
    const cJSON *tm   = cJSON_GetObjectItem(root, "timeout_ms");
    const cJSON *slip = cJSON_GetObjectItem(root, "slip");
    if (!cJSON_IsString(hex)) { cJSON_Delete(root); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no hex"); }
    int timeout_ms = cJSON_IsNumber(tm) ? tm->valueint : 500;
    bool wrap = cJSON_IsBool(slip) && cJSON_IsTrue(slip);

    char *out = mn_raw_tx_ex(hex->valuestring, timeout_ms, wrap);
    cJSON_Delete(root);
    if (!out) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, out, strlen(out));
    free(out);
    return ESP_OK;
}

/* POST /debug/uart-scan?opcode=32&start=0&end=63&ms=100
 * Sweeps a Micronova opcode across a byte-address range and reports which
 * addresses returned a reply. Good for locating matricola (=EEPROM opcode 0x20). */
static esp_err_t handle_debug_uart_scan(httpd_req_t *req)
{
    int opcode = 0x20, start = 0, end = 63, ms = 100;
    char q[64];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char v[16];
        if (httpd_query_key_value(q, "opcode", v, sizeof(v)) == ESP_OK) opcode = (int)strtol(v, NULL, 0);
        if (httpd_query_key_value(q, "start",  v, sizeof(v)) == ESP_OK) start  = (int)strtol(v, NULL, 0);
        if (httpd_query_key_value(q, "end",    v, sizeof(v)) == ESP_OK) end    = (int)strtol(v, NULL, 0);
        if (httpd_query_key_value(q, "ms",     v, sizeof(v)) == ESP_OK) ms     = atoi(v);
    }
    char *out = mn_addr_scan((uint8_t)opcode, (uint8_t)start, (uint8_t)end, ms);
    if (!out) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    SET_NO_CACHE(req);
    httpd_resp_send(req, out, strlen(out));
    free(out);
    return ESP_OK;
}

/* POST /debug/uart-sniffer?ms=5000 - pause master task, capture raw UART bytes,
 * emit one probe ping halfway, return hex dump. */
static esp_err_t handle_debug_uart_sniffer(httpd_req_t *req)
{
    int duration_ms = 5000;
    char q[32];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char v[16];
        if (httpd_query_key_value(q, "ms", v, sizeof(v)) == ESP_OK) {
            duration_ms = atoi(v);
        }
    }
    char *json = mn_sniffer_capture(duration_ms);
    if (!json) return httpd_resp_send_500(req);
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

/* Trim stray trailing punctuation that users often paste along with a URL
 * (=closing parens, brackets, dots, commas). Keeps schemes intact. */
static void sanitize_url(char *s)
{
    size_t n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == ')' || c == ']' || c == '}' || c == ',' || c == '.' ||
            c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'') {
            s[--n] = '\0';
        } else break;
    }
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
    if (url_copy) sanitize_url(url_copy);
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
    hd.max_uri_handlers   = 40;   /* room for the current 34 routes + margin;
                                   * remember to grow this whenever a handler
                                   * is added, ESP-IDF silently drops the
                                   * overflow entries with only a W log line
                                   * that's easy to miss. */
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
        { .uri = "/debug/logs",           .method = HTTP_GET,  .handler = handle_debug_logs,   },
        { .uri = "/debug/ram",            .method = HTTP_GET,  .handler = handle_debug_ram,    },
        { .uri = "/debug/mnstats",        .method = HTTP_GET,  .handler = handle_debug_mnstats,},
        { .uri = "/ota/status",           .method = HTTP_GET,  .handler = handle_ota_status,   },
        { .uri = "/mqtt/discover",        .method = HTTP_GET,  .handler = handle_mqtt_discover,},
        { .uri = "/mqtt/debug",           .method = HTTP_GET,  .handler = handle_mqtt_debug,   },
        { .uri = "/mqtt/test",            .method = HTTP_POST, .handler = handle_mqtt_test,    },
        { .uri = "/debug/uart-sniffer",   .method = HTTP_POST, .handler = handle_debug_uart_sniffer,},
        { .uri = "/debug/uart-tx",        .method = HTTP_POST, .handler = handle_debug_uart_tx,     },
        { .uri = "/debug/uart-scan",      .method = HTTP_POST, .handler = handle_debug_uart_scan,   },
        { .uri = "/debug/uart-set",       .method = HTTP_POST, .handler = handle_debug_uart_set,    },
        { .uri = "/debug/rx-raw",         .method = HTTP_POST, .handler = handle_debug_rx_raw,      },
        { .uri = "/debug/tx-check",       .method = HTTP_POST, .handler = handle_debug_tx_check,    },
        { .uri = "/api/registers",        .method = HTTP_GET,  .handler = handle_registers_live,   },
        { .uri = "/debug/poll-list",      .method = HTTP_GET,  .handler = handle_poll_list,        },
        { .uri = "/debug/poll-list",      .method = HTTP_POST, .handler = handle_poll_list,        },
        { .uri = "/debug/poll-interval",  .method = HTTP_GET,  .handler = handle_poll_interval,    },
        { .uri = "/debug/poll-interval",  .method = HTTP_POST, .handler = handle_poll_interval,    },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "Web UI listening on :80");
    return ESP_OK;
}
