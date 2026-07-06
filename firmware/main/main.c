/**
 * openextraflame - main entry point
 *
 * Custom firmware to replace the cloud dependency of the Extraflame Black Label
 * Wi-Fi module with a local MQTT bridge to Home Assistant.
 *
 * Two targets:
 *  - external   : spare ESP32 board wired to stove UART bus
 *  - blacklabel : replaces original firmware on the module
 *
 * Copyright (c) 2026 Shad107
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "esp_app_desc.h"
#include "esp_partition.h"

#include "hardware_config.h"
#include "config_nvs.h"
#include "wifi_bridge.h"
#include "mqtt_bridge.h"
#include "micronova.h"
#include "web_ui.h"
#include "leds.h"
#include "ota.h"

static const char *TAG = "MAIN";

/* Event group shared across modules for lifecycle signaling */
EventGroupHandle_t app_event_group = NULL;

const int WIFI_STA_CONNECTED_BIT = BIT0;
const int MQTT_CONNECTED_BIT      = BIT1;
const int STOVE_ONLINE_BIT        = BIT2;
const int PROVISIONING_BIT        = BIT3;

static void print_boot_banner(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "  OpenXtraflame %s - build %s %s",
             desc ? desc->version : "?", __DATE__, __TIME__);
    ESP_LOGI(TAG, "  Board       : %s", BOARD_NAME);
#ifdef TARGET_EXTERNAL
    ESP_LOGI(TAG, "  Target      : EXTERNAL (spare ESP32)");
#elif defined(TARGET_BLACKLABEL)
    ESP_LOGI(TAG, "  Target      : BLACKLABEL (reflash original)");
#endif
    ESP_LOGI(TAG, "  Chip        : ESP32");
    ESP_LOGI(TAG, "  IDF version : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "===============================================");
}

/* Wipe the phy_init partition when the firmware version changes so a
 * previous firmware's PHY calibration blob does not linger and confuse
 * the new build's Wi-Fi driver. Session 2026-07-06 saw 6h of reason=210
 * loops that vanished only after a full erase; automating this makes
 * every future OTA safe against sdkconfig Wi-Fi/IRAM/PHY drift.
 *
 * Runs once per new version. Does nothing on the same version reboots.
 */
static void wipe_phy_init_on_version_change(void)
{
    nvs_handle_t nvs;
    if (nvs_open("bootcheck", NVS_READWRITE, &nvs) != ESP_OK) return;

    char stored[48] = {0};
    size_t len = sizeof(stored);
    nvs_get_str(nvs, "last_version", stored, &len);

    const char *current = esp_app_get_description()->version;

    if (strcmp(stored, current) == 0) {
        nvs_close(nvs);
        return;
    }

    ESP_LOGW(TAG, "Firmware version changed ('%s' -> '%s'), wiping phy_init partition",
             stored, current);
    const esp_partition_t *phy = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PHY, NULL);
    if (phy) {
        esp_partition_erase_range(phy, 0, phy->size);
    }
    nvs_set_str(nvs, "last_version", current);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void app_main(void)
{
    /* 1. NVS init (=required by esp_wifi and mqtt) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, doing it now");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 1b. If the firmware version differs from what booted last time, wipe
     *     the phy_init partition so the new driver recalibrates from scratch. */
    wipe_phy_init_on_version_change();

    print_boot_banner();

    /* 2. App event group */
    app_event_group = xEventGroupCreate();
    if (!app_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }

    /* 3. LEDs init */
    leds_init();
    leds_set_state(LED_STATE_BOOT);

    /* 4. Load config from NVS */
    app_config_t cfg;
    config_nvs_load(&cfg);
    ESP_LOGI(TAG, "Config loaded: provisioned=%s, wifi_ssid='%s', mqtt_host='%s'",
             cfg.provisioned ? "yes" : "no",
             cfg.wifi_ssid,
             cfg.mqtt_host);

#ifdef TARGET_BLACKLABEL
    /* 4b. Read the original stove identity from the preserved secret1 partition */
    {
        char secure_code[16] = "";
        char stove_model[16] = "";
        char matricola[16]   = "";
        if (config_nvs_read_stove_secrets(secure_code, sizeof(secure_code),
                                          stove_model, sizeof(stove_model),
                                          matricola,   sizeof(matricola)) == ESP_OK) {
            ESP_LOGI(TAG, "Stove identity (from secret1): secure_code=%s model='%s' matricola=%s",
                     secure_code[0] ? secure_code : "(none)",
                     stove_model,
                     matricola[0] ? matricola : "(none)");
        }
    }
#endif

    /* 4c. Init OTA subsystem (=mutex, marks running app valid) */
    ota_init();

    /* 5. Start Micronova UART task FIRST (=independent of Wi-Fi, works in QEMU) */
    micronova_start();

    /* 6. Start Wi-Fi bridge (=STA if provisioned, else SoftAP)
     * Skipped in QEMU where Wi-Fi isn't supported - Micronova bus still works.
     */
    if (cfg.provisioned) {
        /* Dual APSTA: STA connects to configured Wi-Fi, SoftAP stays up as
         * permanent fallback (=so we can always recover if STA fails). */
        wifi_bridge_start_dual(cfg.wifi_ssid, cfg.wifi_password);
    } else {
        /* Enter provisioning mode - SoftAP only until credentials saved. */
        xEventGroupSetBits(app_event_group, PROVISIONING_BIT);
        wifi_bridge_start_ap();
        leds_set_state(LED_STATE_AP_MODE);
    }

    /* 7. Start web UI (=always, works in AP and STA modes) */
    web_ui_start(&cfg);

    /* 8. Start MQTT bridge (=only if provisioned) */
    if (cfg.provisioned && strlen(cfg.mqtt_host) > 0) {
        /* Wait for Wi-Fi STA connect before MQTT */
        ESP_LOGI(TAG, "Waiting for Wi-Fi STA connect...");
        EventBits_t bits = xEventGroupWaitBits(
            app_event_group,
            WIFI_STA_CONNECTED_BIT,
            pdFALSE, pdTRUE,
            pdMS_TO_TICKS(60000));

        if (bits & WIFI_STA_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Wi-Fi connected, starting MQTT bridge");
            mqtt_bridge_start(&cfg);
        } else {
            ESP_LOGW(TAG, "Wi-Fi timeout, MQTT bridge not started");
        }
    }

    /* 9. Main loop: monitor state, blink LEDs, periodic heartbeat */
    for (;;) {
        EventBits_t bits = xEventGroupGetBits(app_event_group);

        led_state_t state = LED_STATE_UNKNOWN;
        if (bits & PROVISIONING_BIT) {
            state = LED_STATE_AP_MODE;
        } else if ((bits & WIFI_STA_CONNECTED_BIT) && (bits & MQTT_CONNECTED_BIT)) {
            state = (bits & STOVE_ONLINE_BIT) ? LED_STATE_ALL_OK : LED_STATE_STOVE_OFFLINE;
        } else if (bits & WIFI_STA_CONNECTED_BIT) {
            state = LED_STATE_MQTT_OFFLINE;
        } else {
            state = LED_STATE_WIFI_OFFLINE;
        }
        leds_set_state(state);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
