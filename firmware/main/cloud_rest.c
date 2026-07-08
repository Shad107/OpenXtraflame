/**
 * cloud_rest - implémentation
 *
 * Utilise esp_http_client + le CA Omnyvore (=extraflame_ca.pem embedded).
 *
 * NB : le cert appapi.extraflame.it est signé par une CA publique (=Let's
 * Encrypt ou Sectigo), PAS par le CA Omnyvore self-signed. On doit soit
 * embed la CA publique, soit utiliser le crt_bundle_attach par défaut ESP-IDF.
 */

#include <string.h>
#include <stdio.h>
#include "cloud_rest.h"

#ifdef TARGET_BLACKLABEL

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "CLOUDREST";

#define API_BASE "https://appapi.extraflame.it"

/* Callback event : accumule le body dans un buffer statique */
typedef struct { char *buf; size_t cap; size_t len; } collect_t;

static esp_err_t http_evt(esp_http_client_event_t *evt)
{
    collect_t *ctx = (collect_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx && evt->data && evt->data_len > 0) {
        size_t take = evt->data_len;
        if (ctx->len + take + 1 > ctx->cap) take = ctx->cap - ctx->len - 1;
        if (take > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, take);
            ctx->len += take;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

static esp_err_t do_post(const char *url, const char *body,
                          const char *auth_header_value,
                          char *out_body, size_t out_cap, int *out_status)
{
    collect_t ctx = { .buf = out_body, .cap = out_cap, .len = 0 };
    out_body[0] = '\0';
    esp_http_client_config_t cfg = {
        .url                = url,
        .event_handler      = http_evt,
        .user_data          = &ctx,
        .crt_bundle_attach  = esp_crt_bundle_attach,
        .timeout_ms         = 8000,
        .method             = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    esp_http_client_set_header(c, "Content-Type", "application/json");
    if (auth_header_value) esp_http_client_set_header(c, "X-Auth-Token", auth_header_value);
    esp_http_client_set_post_field(c, body, (int)strlen(body));
    esp_err_t err = esp_http_client_perform(c);
    if (out_status) *out_status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    return err;
}

static esp_err_t do_get(const char *url, const char *auth_header_value,
                         char *out_body, size_t out_cap, int *out_status)
{
    collect_t ctx = { .buf = out_body, .cap = out_cap, .len = 0 };
    out_body[0] = '\0';
    esp_http_client_config_t cfg = {
        .url                = url,
        .event_handler      = http_evt,
        .user_data          = &ctx,
        .crt_bundle_attach  = esp_crt_bundle_attach,
        .timeout_ms         = 8000,
        .method             = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    if (auth_header_value) esp_http_client_set_header(c, "X-Auth-Token", auth_header_value);
    esp_err_t err = esp_http_client_perform(c);
    if (out_status) *out_status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    return err;
}

esp_err_t cloud_rest_fetch_stove_info(const char *username,
                                       const char *password,
                                       const char *my_matricola,
                                       char *out_stove_id, size_t sid_len,
                                       char *out_stove_model, size_t smod_len)
{
    if (!username || !username[0] || !password || !password[0] || !my_matricola) {
        return ESP_ERR_INVALID_ARG;
    }
    static char body[2048];
    int status = 0;

    /* 1) Login */
    char auth_body[256];
    snprintf(auth_body, sizeof(auth_body),
             "{\"username\":\"%s\",\"password\":\"%s\"}", username, password);
    esp_err_t err = do_post(API_BASE "/auth", auth_body, NULL, body, sizeof(body), &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "login failed err=%d status=%d body=%.100s", err, status, body);
        return ESP_FAIL;
    }
    cJSON *o = cJSON_Parse(body);
    if (!o) return ESP_FAIL;
    cJSON *tk = cJSON_GetObjectItem(o, "token");
    if (!cJSON_IsString(tk)) { cJSON_Delete(o); return ESP_FAIL; }
    char token[600];
    strncpy(token, tk->valuestring, sizeof(token) - 1);
    token[sizeof(token) - 1] = '\0';
    cJSON_Delete(o);
    ESP_LOGI(TAG, "Login OK, JWT len=%d", (int)strlen(token));

    /* 2) GET /stoves */
    err = do_get(API_BASE "/stoves", token, body, sizeof(body), &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "GET /stoves failed err=%d status=%d", err, status);
        return ESP_FAIL;
    }
    o = cJSON_Parse(body);
    if (!o) return ESP_FAIL;
    cJSON *data = cJSON_GetObjectItem(o, "data");
    if (!cJSON_IsArray(data)) { cJSON_Delete(o); return ESP_FAIL; }
    /* Find matching stove by resourceId == my_matricola */
    bool found = false;
    cJSON *s;
    cJSON_ArrayForEach(s, data) {
        cJSON *rid = cJSON_GetObjectItem(s, "resourceId");
        if (cJSON_IsString(rid) && strcmp(rid->valuestring, my_matricola) == 0) {
            cJSON *id = cJSON_GetObjectItem(s, "id");
            /* stove_model n'est PAS dans /stoves. On récupère le model
             * réel Omnyvore ailleurs (=via un endpoint qui expose le
             * requesttopic). Pour l'instant on stocke uniquement le id. */
            if (cJSON_IsString(id)) {
                strncpy(out_stove_id, id->valuestring, sid_len - 1);
                out_stove_id[sid_len - 1] = '\0';
                found = true;
                ESP_LOGI(TAG, "Stove found: id=%s", out_stove_id);
            }
            break;
        }
    }
    cJSON_Delete(o);
    if (!found) {
        ESP_LOGW(TAG, "Aucun poêle avec resourceId=%s trouvé sur ce compte", my_matricola);
        return ESP_ERR_NOT_FOUND;
    }

    /* 3) Récupérer le stove_model réel via sendCommand qui expose le requesttopic
     * dans la réponse. On envoie un status "vide" pour trigger sans modifier
     * l'état du poêle. */
    char cmd_url[128];
    snprintf(cmd_url, sizeof(cmd_url),
             API_BASE "/stoves/%s/sendCommand/status", out_stove_id);
    err = do_post(cmd_url, "{}", token, body, sizeof(body), &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "sendCommand for model detection failed status=%d, fallback vide", status);
        out_stove_model[0] = '\0';
        return ESP_OK;  /* stove_id trouvé, model TBD */
    }
    /* Parse requesttopic pour extraire le model.
     * Format : "omv/ex/<MAC>/<model> 1.8/<matricola>/IN/...". */
    o = cJSON_Parse(body);
    if (o) {
        cJSON *dd = cJSON_GetObjectItem(o, "data");
        if (dd) {
            cJSON *rt = cJSON_GetObjectItem(dd, "requesttopic");
            if (cJSON_IsString(rt)) {
                /* Format : omv/ex/<MAC>/<model> 1.8/<mat>/IN/...
                 * Prendre le 4e segment (=index 3 après split par '/'),
                 * puis couper au 1er espace ou / trouvé. */
                const char *p = rt->valuestring;
                int slash = 0;
                while (*p && slash < 3) { if (*p == '/') slash++; p++; }
                if (slash == 3) {
                    const char *end = p;
                    while (*end && *end != ' ' && *end != '/') end++;
                    size_t n = end - p;
                    if (n > 0 && n < smod_len) {
                        memcpy(out_stove_model, p, n);
                        out_stove_model[n] = '\0';
                        ESP_LOGI(TAG, "Stove model extrait: %s", out_stove_model);
                    }
                }
            }
        }
        cJSON_Delete(o);
    }
    return ESP_OK;
}

#endif /* TARGET_BLACKLABEL */
