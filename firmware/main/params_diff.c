#include "params_diff.h"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "micronova.h"
#include <string.h>
#include <time.h>

static const char *TAG = "params_diff";

static uint8_t s_baseline[PARAMS_DIFF_COUNT];
static bool s_has_baseline = false;

/* Watcher passif : ring buffer 32 mutations spontanées */
#define WATCHER_SIZE 32
typedef struct {
    uint8_t  addr;
    uint8_t  before;
    uint8_t  after;
    uint32_t ts;
} params_watch_event_t;
static params_watch_event_t s_watch_ring[WATCHER_SIZE];
static uint8_t s_watch_head = 0;
static uint8_t s_watch_count = 0;
static uint8_t s_watch_last[PARAMS_DIFF_COUNT];
static bool    s_watch_init = false;

static void watcher_save(void)
{
    nvs_handle_t h;
    if (nvs_open("pwatch", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "ring", s_watch_ring, sizeof(s_watch_ring));
    nvs_set_u8(h, "head", s_watch_head);
    nvs_set_u8(h, "cnt",  s_watch_count);
    nvs_commit(h);
    nvs_close(h);
}

void params_diff_init(void)
{
    memset(s_baseline, 0, sizeof(s_baseline));
    memset(s_watch_last, 0, sizeof(s_watch_last));
    s_has_baseline = false;
    s_watch_init = false;
    nvs_handle_t h;
    if (nvs_open("pwatch", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_watch_ring);
        nvs_get_blob(h, "ring", s_watch_ring, &sz);
        nvs_get_u8(h, "head", &s_watch_head);
        nvs_get_u8(h, "cnt",  &s_watch_count);
        nvs_close(h);
        if (s_watch_head >= WATCHER_SIZE) s_watch_head = 0;
        if (s_watch_count > WATCHER_SIZE) s_watch_count = WATCHER_SIZE;
        ESP_LOGI(TAG, "watcher restauré %u mutations", (unsigned)s_watch_count);
    }
}

void params_diff_watcher_tick(void)
{
    if (!s_watch_init) {
        for (int i = 0; i < PARAMS_DIFF_COUNT; i++) {
            s_watch_last[i] = mn_get_ram(0x100 + PARAMS_DIFF_BASE + i);
        }
        s_watch_init = true;
        return;
    }
    time_t now = 0;
    time(&now);
    bool changed = false;
    for (int i = 0; i < PARAMS_DIFF_COUNT; i++) {
        uint8_t v = mn_get_ram(0x100 + PARAMS_DIFF_BASE + i);
        if (v != s_watch_last[i]) {
            s_watch_ring[s_watch_head].addr   = PARAMS_DIFF_BASE + i;
            s_watch_ring[s_watch_head].before = s_watch_last[i];
            s_watch_ring[s_watch_head].after  = v;
            s_watch_ring[s_watch_head].ts     = (now > 1700000000) ? (uint32_t)now : 0;
            s_watch_head = (s_watch_head + 1) % WATCHER_SIZE;
            if (s_watch_count < WATCHER_SIZE) s_watch_count++;
            ESP_LOGW(TAG, "MUTATION 0x%02x : %u -> %u",
                PARAMS_DIFF_BASE + i, s_watch_last[i], v);
            s_watch_last[i] = v;
            changed = true;
        }
    }
    if (changed) watcher_save();
}

char *params_diff_watcher_dump_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    uint8_t start = (s_watch_count < WATCHER_SIZE) ? 0 : s_watch_head;
    for (int i = 0; i < s_watch_count; i++) {
        uint8_t idx = (start + i) % WATCHER_SIZE;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "addr",   s_watch_ring[idx].addr);
        cJSON_AddNumberToObject(o, "before", s_watch_ring[idx].before);
        cJSON_AddNumberToObject(o, "after",  s_watch_ring[idx].after);
        cJSON_AddNumberToObject(o, "ts",     s_watch_ring[idx].ts);
        cJSON_AddItemToArray(arr, o);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}

void params_diff_snapshot(void)
{
    for (int i = 0; i < PARAMS_DIFF_COUNT; i++) {
        s_baseline[i] = mn_get_ram(0x100 + PARAMS_DIFF_BASE + i);
    }
    s_has_baseline = true;
    ESP_LOGI(TAG, "baseline pris sur %d registres", PARAMS_DIFF_COUNT);
}

uint8_t params_diff_count(void)
{
    if (!s_has_baseline) return 0;
    uint8_t c = 0;
    for (int i = 0; i < PARAMS_DIFF_COUNT; i++) {
        if (s_baseline[i] != mn_get_ram(0x100 + PARAMS_DIFF_BASE + i)) c++;
    }
    return c;
}

char *params_diff_json(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddBoolToObject(o, "has_baseline", s_has_baseline);
    if (s_has_baseline) {
        for (int i = 0; i < PARAMS_DIFF_COUNT; i++) {
            uint8_t now = mn_get_ram(0x100 + PARAMS_DIFF_BASE + i);
            if (s_baseline[i] != now) {
                cJSON *d = cJSON_CreateObject();
                cJSON_AddNumberToObject(d, "addr",   PARAMS_DIFF_BASE + i);
                cJSON_AddNumberToObject(d, "before", s_baseline[i]);
                cJSON_AddNumberToObject(d, "after",  now);
                cJSON_AddNumberToObject(d, "delta",  (int)now - (int)s_baseline[i]);
                cJSON_AddItemToArray(arr, d);
            }
        }
    }
    cJSON_AddItemToObject(o, "changes", arr);
    char *out = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return out;
}
