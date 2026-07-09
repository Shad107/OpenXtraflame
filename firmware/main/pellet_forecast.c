#include "pellet_forecast.h"
#include "nvs.h"
#include "esp_log.h"
#include <time.h>

static const char *TAG = "pellet_forecast";
static const char *NVS_NS = "pellfc";

#define WINDOW_DAYS 7
static pellet_daily_t s_win[WINDOW_DAYS];
static uint8_t s_head = 0;
static uint8_t s_count = 0;
static uint32_t s_last_snap_ts = 0;

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "win", s_win, sizeof(s_win));
    nvs_set_u8(h,   "head", s_head);
    nvs_set_u8(h,   "cnt",  s_count);
    nvs_set_u32(h,  "last", s_last_snap_ts);
    nvs_commit(h);
    nvs_close(h);
}

void pellet_forecast_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t sz = sizeof(s_win);
    nvs_get_blob(h, "win", s_win, &sz);
    nvs_get_u8(h,  "head", &s_head);
    nvs_get_u8(h,  "cnt",  &s_count);
    nvs_get_u32(h, "last", &s_last_snap_ts);
    nvs_close(h);
    if (s_head >= WINDOW_DAYS) s_head = 0;
    if (s_count > WINDOW_DAYS) s_count = WINDOW_DAYS;
    ESP_LOGI(TAG, "restored %u snapshots", (unsigned)s_count);
}

void pellet_forecast_tick(float since_refill_kg)
{
    time_t now = 0;
    time(&now);
    if (now < 1700000000) return;

    if (s_last_snap_ts == 0 || (uint32_t)now - s_last_snap_ts >= 86400) {
        s_win[s_head].kg_snapshot = since_refill_kg;
        s_win[s_head].ts = (uint32_t)now;
        s_head = (s_head + 1) % WINDOW_DAYS;
        if (s_count < WINDOW_DAYS) s_count++;
        s_last_snap_ts = (uint32_t)now;
        save();
        ESP_LOGI(TAG, "snapshot #%u kg=%.2f ts=%u",
            (unsigned)s_count, since_refill_kg, (unsigned)now);
    }
}

float pellet_forecast_kg_per_day(void)
{
    if (s_count < 2) return 0.0f;
    uint8_t oldest = (s_head + WINDOW_DAYS - s_count) % WINDOW_DAYS;
    uint8_t newest = (s_head + WINDOW_DAYS - 1) % WINDOW_DAYS;
    float dkg = s_win[newest].kg_snapshot - s_win[oldest].kg_snapshot;
    uint32_t dt = s_win[newest].ts - s_win[oldest].ts;
    if (dt < 3600 || dkg <= 0.01f) return 0.0f;
    float days = (float)dt / 86400.0f;
    return dkg / days;
}

float pellet_forecast_days_left(float remaining_kg)
{
    float rate = pellet_forecast_kg_per_day();
    if (rate <= 0.01f) return 0.0f;
    if (remaining_kg <= 0.0f) return 0.0f;
    return remaining_kg / rate;
}

uint32_t pellet_forecast_empty_ts(float remaining_kg)
{
    float days = pellet_forecast_days_left(remaining_kg);
    if (days <= 0.0f) return 0;
    time_t now = 0;
    time(&now);
    if (now < 1700000000) return 0;
    return (uint32_t)now + (uint32_t)(days * 86400.0f);
}
