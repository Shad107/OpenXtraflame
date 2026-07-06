/**
 * openextraflame - OTA implementation
 */

#include <string.h>
#include "ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "OTA";

static SemaphoreHandle_t ota_mutex;
static ota_status_t status;
static esp_ota_handle_t update_handle;
static const esp_partition_t *update_partition;

esp_err_t ota_init(void)
{
    ota_mutex = xSemaphoreCreateMutex();
    memset(&status, 0, sizeof(status));
    status.state = OTA_STATE_IDLE;

    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc) {
        strncpy(status.active_version, desc->version, sizeof(status.active_version) - 1);
    }

    /* Mark current app valid on first successful boot */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t s;
    if (esp_ota_get_state_partition(running, &s) == ESP_OK) {
        if (s == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Marking firmware as valid (=first successful boot)");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
    return ESP_OK;
}

void ota_get_status(ota_status_t *out)
{
    if (xSemaphoreTake(ota_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        memcpy(out, &status, sizeof(status));
        xSemaphoreGive(ota_mutex);
    }
}

esp_err_t ota_upload_begin(size_t total_bytes)
{
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) return ESP_FAIL;

    esp_err_t err = esp_ota_begin(update_partition, total_bytes, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_RECEIVING;
    status.total_bytes = total_bytes;
    status.written_bytes = 0;
    strncpy(status.message, "receiving", sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);
    return ESP_OK;
}

esp_err_t ota_upload_data(const void *data, size_t len)
{
    esp_err_t err = esp_ota_write(update_handle, data, len);
    if (err != ESP_OK) return err;

    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.written_bytes += len;
    xSemaphoreGive(ota_mutex);
    return ESP_OK;
}

esp_err_t ota_upload_end(void)
{
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_VERIFYING;
    strncpy(status.message, "verifying", sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);

    esp_err_t err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition failed: %s", esp_err_to_name(err));
        goto fail;
    }

    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_REBOOTING;
    strncpy(status.message, "rebooting", sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);

    ESP_LOGI(TAG, "OTA success, rebooting in 2s...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;

fail:
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_FAILED;
    strncpy(status.message, esp_err_to_name(err), sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);
    return err;
}

esp_err_t ota_upload_abort(void)
{
    esp_ota_abort(update_handle);
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_IDLE;
    strncpy(status.message, "aborted", sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);
    return ESP_OK;
}

esp_err_t ota_pull_from_url(const char *url)
{
    /* crt_bundle_attach lets esp_https_ota validate any HTTPS server whose
     * CA is in the ESP-IDF-embedded Mozilla CA bundle. Without this,
     * esp_https_ota refuses HTTPS URLs with 'No option for server
     * verification is enabled'. Falls back to plain HTTP silently if the
     * URL is http://. */
    esp_http_client_config_t http_cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };

    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_RECEIVING;
    strncpy(status.message, url, sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA pull success, rebooting");
        esp_restart();
    } else {
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        status.state = OTA_STATE_FAILED;
        strncpy(status.message, esp_err_to_name(err), sizeof(status.message) - 1);
        xSemaphoreGive(ota_mutex);
    }
    return err;
}

esp_err_t ota_rollback(void)
{
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    return err;
}

esp_err_t ota_mark_valid(void)
{
    return esp_ota_mark_app_valid_cancel_rollback();
}
