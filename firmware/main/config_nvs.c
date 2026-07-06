/**
 * openextraflame - NVS-backed configuration
 */

#include <string.h>
#include "config_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define NVS_NAMESPACE  "openxflame"
#define KEY_PROVISIONED       "prov"
#define KEY_WIFI_SSID         "wssid"
#define KEY_WIFI_PWD          "wpwd"
#define KEY_MQTT_HOST         "mhost"
#define KEY_MQTT_PORT         "mport"
#define KEY_MQTT_USER         "muser"
#define KEY_MQTT_PWD          "mpwd"
#define KEY_MQTT_PREFIX       "mprefix"
#define KEY_MQTT_TLS          "mtls"
#define KEY_STOVE_TYPE        "stype"
#define KEY_STOVE_NAME        "sname"
#define KEY_HA_DISCOVERY      "ha_disc"
#define KEY_PUBLISH_INTERVAL  "pub_ms"

static const char *TAG = "CFG";

void config_nvs_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->provisioned          = false;
    cfg->mqtt_port            = 1883;
    strncpy(cfg->mqtt_topic_prefix, "extraflame", sizeof(cfg->mqtt_topic_prefix) - 1);
    strncpy(cfg->stove_name, "poele", sizeof(cfg->stove_name) - 1);
    cfg->mqtt_use_tls         = false;
    cfg->stove_type           = STOVE_TYPE_UNKNOWN;
    cfg->ha_discovery_enabled = true;
    cfg->publish_interval_ms  = 5000;
}

static esp_err_t get_str_or_default(nvs_handle_t h, const char *key, char *out, size_t max_len)
{
    size_t len = max_len;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; // keep default
    }
    return err;
}

esp_err_t config_nvs_load(app_config_t *cfg)
{
    config_nvs_defaults(cfg);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No config in NVS, using defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t u8 = 0;
    if (nvs_get_u8(h, KEY_PROVISIONED, &u8) == ESP_OK) {
        cfg->provisioned = (u8 != 0);
    }
    get_str_or_default(h, KEY_WIFI_SSID, cfg->wifi_ssid, sizeof(cfg->wifi_ssid));
    get_str_or_default(h, KEY_WIFI_PWD, cfg->wifi_password, sizeof(cfg->wifi_password));

    get_str_or_default(h, KEY_MQTT_HOST, cfg->mqtt_host, sizeof(cfg->mqtt_host));
    nvs_get_u16(h, KEY_MQTT_PORT, &cfg->mqtt_port);
    get_str_or_default(h, KEY_MQTT_USER, cfg->mqtt_username, sizeof(cfg->mqtt_username));
    get_str_or_default(h, KEY_MQTT_PWD, cfg->mqtt_password, sizeof(cfg->mqtt_password));
    get_str_or_default(h, KEY_MQTT_PREFIX, cfg->mqtt_topic_prefix, sizeof(cfg->mqtt_topic_prefix));
    if (nvs_get_u8(h, KEY_MQTT_TLS, &u8) == ESP_OK) {
        cfg->mqtt_use_tls = (u8 != 0);
    }

    int32_t i32 = 0;
    if (nvs_get_i32(h, KEY_STOVE_TYPE, &i32) == ESP_OK) {
        cfg->stove_type = (stove_type_t)i32;
    }
    get_str_or_default(h, KEY_STOVE_NAME, cfg->stove_name, sizeof(cfg->stove_name));

    if (nvs_get_u8(h, KEY_HA_DISCOVERY, &u8) == ESP_OK) {
        cfg->ha_discovery_enabled = (u8 != 0);
    }
    nvs_get_u16(h, KEY_PUBLISH_INTERVAL, &cfg->publish_interval_ms);

    nvs_close(h);
    return ESP_OK;
}

esp_err_t config_nvs_save(const app_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open rw failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_u8(h, KEY_PROVISIONED, cfg->provisioned ? 1 : 0);
    nvs_set_str(h, KEY_WIFI_SSID, cfg->wifi_ssid);
    nvs_set_str(h, KEY_WIFI_PWD, cfg->wifi_password);
    nvs_set_str(h, KEY_MQTT_HOST, cfg->mqtt_host);
    nvs_set_u16(h, KEY_MQTT_PORT, cfg->mqtt_port);
    nvs_set_str(h, KEY_MQTT_USER, cfg->mqtt_username);
    nvs_set_str(h, KEY_MQTT_PWD, cfg->mqtt_password);
    nvs_set_str(h, KEY_MQTT_PREFIX, cfg->mqtt_topic_prefix);
    nvs_set_u8(h, KEY_MQTT_TLS, cfg->mqtt_use_tls ? 1 : 0);
    nvs_set_i32(h, KEY_STOVE_TYPE, (int32_t)cfg->stove_type);
    nvs_set_str(h, KEY_STOVE_NAME, cfg->stove_name);
    nvs_set_u8(h, KEY_HA_DISCOVERY, cfg->ha_discovery_enabled ? 1 : 0);
    nvs_set_u16(h, KEY_PUBLISH_INTERVAL, cfg->publish_interval_ms);

    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t config_nvs_reset(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_all(h);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

#ifdef TARGET_BLACKLABEL

/* Original Extraflame partition/namespaces (=reverse engineered from dump) */
#define SECRET_PARTITION    "secret1"
#define SECRET_NS_PRODUCT   "product"     /* secure_code, stove_model */
#define SECRET_NS_COLLAUDO  "collaudo"    /* matricola (=serial number) */

/* Read one string key from a namespace of the secret1 partition.
 * Leaves *out as empty string if the key is missing/blank. */
static void secret_get_str(const char *ns, const char *key, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';

    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(SECRET_PARTITION, ns, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "secret1: open ns '%s' failed: %s", ns, esp_err_to_name(err));
        return;
    }

    size_t len = out_len;
    err = nvs_get_str(h, key, out, &len);
    if (err != ESP_OK) {
        out[0] = '\0';   /* keep empty on NOT_FOUND / errors */
        ESP_LOGD(TAG, "secret1: key '%s/%s' not read: %s", ns, key, esp_err_to_name(err));
    }
    nvs_close(h);
}

esp_err_t config_nvs_read_stove_secrets(char *secure_code, size_t secure_code_len,
                                        char *stove_model, size_t stove_model_len,
                                        char *matricola,   size_t matricola_len)
{
    /* Mount the preserved Extraflame partition as a secondary NVS store. */
    esp_err_t err = nvs_flash_init_partition(SECRET_PARTITION);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "secret1 partition not available: %s", esp_err_to_name(err));
        return err;
    }

    secret_get_str(SECRET_NS_PRODUCT,  "secure_code", secure_code, secure_code_len);
    secret_get_str(SECRET_NS_PRODUCT,  "stove_model", stove_model, stove_model_len);
    secret_get_str(SECRET_NS_COLLAUDO, "matricola",   matricola,   matricola_len);

    nvs_flash_deinit_partition(SECRET_PARTITION);
    return ESP_OK;
}

#endif /* TARGET_BLACKLABEL */
