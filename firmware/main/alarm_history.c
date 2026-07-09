#include "alarm_history.h"
#include "cJSON.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>

static const char *TAG = "alarm_hist";
static const char *NVS_NS = "alarmhist";
static const char *NVS_KEY_BUF = "buf";
static const char *NVS_KEY_HEAD = "head";
static const char *NVS_KEY_CNT  = "cnt";

static alarm_event_t s_buf[ALARM_HISTORY_SIZE];
static uint16_t s_head = 0;
static uint16_t s_count = 0;
static uint8_t  s_last_code = 0;

static void persist(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY_BUF, s_buf, sizeof(s_buf));
    nvs_set_u16(h, NVS_KEY_HEAD, s_head);
    nvs_set_u16(h, NVS_KEY_CNT, s_count);
    nvs_commit(h);
    nvs_close(h);
}

void alarm_history_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t sz = sizeof(s_buf);
    nvs_get_blob(h, NVS_KEY_BUF, s_buf, &sz);
    nvs_get_u16(h, NVS_KEY_HEAD, &s_head);
    nvs_get_u16(h, NVS_KEY_CNT, &s_count);
    nvs_close(h);
    if (s_head >= ALARM_HISTORY_SIZE) s_head = 0;
    if (s_count > ALARM_HISTORY_SIZE) s_count = ALARM_HISTORY_SIZE;
    ESP_LOGI(TAG, "restored %u events", (unsigned)s_count);
}

void alarm_history_on_code_change(uint8_t new_code, uint32_t now_epoch)
{
    if (new_code == s_last_code) return;

    if (s_last_code != 0 && s_count > 0) {
        uint16_t last_idx = (s_head == 0) ? (ALARM_HISTORY_SIZE - 1) : (s_head - 1);
        if (s_buf[last_idx].ts_end == 0) {
            s_buf[last_idx].ts_end = now_epoch;
        }
    }

    if (new_code != 0) {
        s_buf[s_head].code = new_code;
        s_buf[s_head].ts_start = now_epoch;
        s_buf[s_head].ts_end = 0;
        s_head = (s_head + 1) % ALARM_HISTORY_SIZE;
        if (s_count < ALARM_HISTORY_SIZE) s_count++;
    }

    s_last_code = new_code;
    persist();
}

char *alarm_history_dump_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    uint16_t start = (s_count < ALARM_HISTORY_SIZE)
        ? 0
        : s_head;
    for (uint16_t i = 0; i < s_count; i++) {
        uint16_t idx = (start + i) % ALARM_HISTORY_SIZE;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "code", s_buf[idx].code);
        cJSON_AddNumberToObject(o, "ts_start", s_buf[idx].ts_start);
        cJSON_AddNumberToObject(o, "ts_end", s_buf[idx].ts_end);
        if (s_buf[idx].ts_end > s_buf[idx].ts_start) {
            cJSON_AddNumberToObject(o, "duration_s",
                s_buf[idx].ts_end - s_buf[idx].ts_start);
        }
        cJSON_AddItemToArray(arr, o);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}

uint16_t alarm_history_count(void) { return s_count; }
