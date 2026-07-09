#pragma once
#include <stdint.h>

typedef struct {
    float    kg_snapshot;
    uint32_t ts;
} pellet_daily_t;

void  pellet_forecast_init(void);
/* À appeler à chaque cycle publish - throttled 24h en interne */
void  pellet_forecast_tick(float pellets_since_refill_kg);
/* Retourne kg/jour EMA (0 si pas encore assez de données) */
float pellet_forecast_kg_per_day(void);
/* Days-left = remaining_kg / kg_per_day (0 si indéfini) */
float pellet_forecast_days_left(float remaining_kg);
/* Timestamp unix epoch estimé de fin de trémie (0 si indéfini) */
uint32_t pellet_forecast_empty_ts(float remaining_kg);
