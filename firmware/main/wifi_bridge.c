/**
 * openextraflame - Wi-Fi manager implementation
 *
 * Minimal fork of tonyp7/esp32-wifi-manager patterns, no external dep.
 */

#include <string.h>
#include <stdio.h>
#include "wifi_bridge.h"
#include "hardware_config.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include "mdns.h"

/* --- mDNS discovery of the MQTT broker on the local network --- */
/* Multi-strategy lookup. Home Assistant Mosquitto addon does not publish
 * _mqtt._tcp by default (=only some standalone Mosquitto builds do), but
 * HA itself publishes _home-assistant._tcp and the HA OS hostname
 * 'homeassistant.local'. We try, in order:
 *
 *   1. _mqtt._tcp        (=if Mosquitto is configured to advertise)
 *   2. _home-assistant._tcp -> use its IP with port 1883 assumption
 *   3. mdns_query_a("homeassistant") for the plain hostname
 *
 * Returns "IP:port" in `out` (max size 48) on any success, empty on fail.
 * Total budget ~4.5 s. */
static bool try_ptr(const char *type, const char *proto, uint16_t default_port,
                    char *out, size_t out_size)
{
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(type, proto, 1500, 5, &results);
    if (err != ESP_OK || !results) return false;
    bool found = false;
    for (mdns_result_t *r = results; r; r = r->next) {
        if (r->addr && r->addr->addr.type == ESP_IPADDR_TYPE_V4) {
            uint16_t port = r->port > 0 ? r->port : default_port;
            snprintf(out, out_size, IPSTR ":%d",
                     IP2STR(&r->addr->addr.u_addr.ip4), port);
            found = true;
            break;
        }
    }
    mdns_query_results_free(results);
    return found;
}

void wifi_bridge_mdns_query_mqtt(char *out, size_t out_size)
{
    out[0] = '\0';
    static bool mdns_started = false;
    if (!mdns_started) {
        if (mdns_init() != ESP_OK) return;
        mdns_hostname_set("openxtraflame");
        mdns_started = true;
    }
    /* 1. Direct MQTT service (=SRV port trusted, this record IS the broker) */
    if (try_ptr("_mqtt", "_tcp", 1883, out, out_size)) return;

    /* 2. Plain hostname lookup for homeassistant.local (=HA OS advertises
     *    this hostname, and the broker on the Mosquitto add-on runs on the
     *    same IP, port 1883). We deliberately DO NOT chase _home-assistant.
     *    _tcp because its SRV record advertises HA HTTP on port 8123 and
     *    frequently returns a Docker-internal IP that VLAN 20 clients
     *    cannot reach. Beta11 users hit that trap. */
    esp_ip4_addr_t ip = {0};
    if (mdns_query_a("homeassistant", 1500, &ip) == ESP_OK) {
        snprintf(out, out_size, IPSTR ":1883", IP2STR(&ip));
        return;
    }

    /* 3. Same lookup, in case HA published as 'home-assistant' with a dash */
    if (mdns_query_a("home-assistant", 1500, &ip) == ESP_OK) {
        snprintf(out, out_size, IPSTR ":1883", IP2STR(&ip));
    }
}

extern EventGroupHandle_t app_event_group;
extern const int WIFI_STA_CONNECTED_BIT;

static const char *TAG = "WIFI";

static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif  = NULL;
static char curr_ssid[64] = "";
static char curr_ip[16]   = "";

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "STA disconnected reason=%d (=see esp_wifi_types.h wifi_err_reason_t), reconnecting in 5s...", ev->reason);
            xEventGroupClearBits(app_event_group, WIFI_STA_CONNECTED_BIT);
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_wifi_connect();
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "AP: client connected");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "AP: client disconnected");
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(curr_ip, sizeof(curr_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "STA got IP %s", curr_ip);
        xEventGroupSetBits(app_event_group, WIFI_STA_CONNECTED_BIT);
    }
}

static void wifi_init_common(void)
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    /* Crank the Wi-Fi driver's log level to VERBOSE so users can see the
     * full scan -> auth -> assoc transition instead of just the final
     * "reason=X" code. Costs some UART bandwidth but is invaluable when
     * a bare reason code doesn't point at the true failure mode. */
    esp_log_level_set("wifi",      ESP_LOG_VERBOSE);
    esp_log_level_set("wifi_init", ESP_LOG_VERBOSE);
    esp_log_level_set("phy",       ESP_LOG_VERBOSE);
    esp_log_level_set("net80211",  ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    /* Explicit country: some ESP-IDF defaults ship with a restricted channel
     * set that excludes channels used by EU APs (=13). Setting FR/EU with
     * policy AUTO lets us honor the AP's country IE at association time and
     * still scan channels 1..13. Without this, reason=210 can happen when
     * the AP beacons on ch12/13 but the STA refuses to associate. */
    wifi_country_t country = {
        .cc = "FR",
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 20,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));
}

esp_err_t wifi_bridge_start_ap(void)
{
    wifi_init_common();

    ap_netif = esp_netif_create_default_wifi_ap();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    wifi_config_t wcfg = {0};
    /* Use a temporary buffer wide enough to avoid -Wformat-truncation */
    char ssid_tmp[64];
    snprintf(ssid_tmp, sizeof(ssid_tmp),
             "%s%02X%02X%02X%02X%02X%02X",
             AP_SSID_PREFIX, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    strncpy((char *)wcfg.ap.ssid, ssid_tmp, sizeof(wcfg.ap.ssid) - 1);
    wcfg.ap.ssid_len       = strlen((char *)wcfg.ap.ssid);
    wcfg.ap.channel        = 5;
    wcfg.ap.authmode       = WIFI_AUTH_OPEN;
    wcfg.ap.max_connection = 4;

    /* APSTA (=AP + STA simultanés) au lieu de AP-only, pour que esp_wifi_scan_start()
     * puisse lister les Wi-Fi environnants pendant que le SoftAP reste up.
     * En mode AP-only, esp_wifi_scan_start renvoie ESP_ERR_WIFI_MODE. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP up : SSID='%s' (open) IP=192.168.4.1", wcfg.ap.ssid);
    return ESP_OK;
}

/* Dual APSTA: SoftAP stays up permanently as a fallback (=user can always
 * reconnect to openxtraflame-XXXX to fix Wi-Fi credentials if the STA fails
 * to obtain a DHCP lease from the configured router). Cost is ~20KB of Wi-Fi
 * driver overhead, worth it for the never-brick guarantee. */
esp_err_t wifi_bridge_start_dual(const char *ssid, const char *password)
{
    wifi_init_common();
    ap_netif  = esp_netif_create_default_wifi_ap();
    sta_netif = esp_netif_create_default_wifi_sta();

    /* --- SoftAP config (=fallback access point) --- */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap_cfg = {0};
    char ssid_tmp[64];
    snprintf(ssid_tmp, sizeof(ssid_tmp),
             "%s%02X%02X%02X%02X%02X%02X",
             AP_SSID_PREFIX, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    strncpy((char *)ap_cfg.ap.ssid, ssid_tmp, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len       = strlen((char *)ap_cfg.ap.ssid);
    /* channel=0 => the driver will pick a channel matching the STA once
     * associated. Forcing a fixed channel (=e.g. 5) in APSTA mode creates
     * a hard conflict when the STA's AP is on a different channel:
     * ESP32 has only one Wi-Fi radio, and the SoftAP holds its channel
     * while the STA needs to move to the AP's. Result: reason=210 loop
     * introduced by the beta11 dual-mode change. */
    ap_cfg.ap.channel        = 0;
    ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;

    /* --- STA config (=connect to user's Wi-Fi) --- */
    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1);
    strncpy(curr_ssid, ssid, sizeof(curr_ssid) - 1);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable    = true;
    sta_cfg.sta.pmf_cfg.required   = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,  &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Dual mode : SoftAP='%s' (open) + STA connecting to '%s'",
             ap_cfg.ap.ssid, ssid);
    return ESP_OK;
}

esp_err_t wifi_bridge_start_sta(const char *ssid, const char *password)
{
    /* Legacy entry point kept for source compat; forwards to dual mode. */
    return wifi_bridge_start_dual(ssid, password);
}

char *wifi_bridge_scan_json(void)
{
    /* PASSIVE scan with a short per-channel dwell: an active all-channel scan
     * blocks this (single) httpd task for 13-39 s, wedging the whole web UI.
     * Passive @120ms/channel completes in ~1.5 s. */
    wifi_scan_config_t scan_cfg = {
        .show_hidden        = false,
        .scan_type          = WIFI_SCAN_TYPE_PASSIVE,
        .scan_time.passive  = 120,
    };
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        return cJSON_PrintUnformatted(cJSON_CreateArray());  /* empty [] on error */
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num > 20) num = 20;

    wifi_ap_record_t records[20];
    esp_wifi_scan_get_ap_records(&num, records);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < num; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char *)records[i].ssid);
        cJSON_AddNumberToObject(ap, "chan", records[i].primary);
        cJSON_AddNumberToObject(ap, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(ap, "auth", records[i].authmode);
        cJSON_AddItemToArray(arr, ap);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}

char *wifi_bridge_status_json(void)
{
    cJSON *o = cJSON_CreateObject();
    EventBits_t bits = xEventGroupGetBits(app_event_group);
    cJSON_AddBoolToObject(o, "connected", (bits & WIFI_STA_CONNECTED_BIT) != 0);
    cJSON_AddStringToObject(o, "ssid", curr_ssid);
    cJSON_AddStringToObject(o, "ip", curr_ip);
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return out;
}
