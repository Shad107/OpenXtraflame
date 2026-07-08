/**
 * cloud_rest - Client REST vers appapi.extraflame.it
 *
 * Login (POST /auth) → récupère un JWT
 * GET /stoves        → récupère stove_id interne + stove_model Omnyvore
 *
 * TARGET_BLACKLABEL uniquement.
 */
#pragma once

#include "esp_err.h"

#ifdef TARGET_BLACKLABEL

/* Login puis récupère info du poêle qui matche notre matricola.
 * Écrit dans out_stove_id et out_stove_model (size min = 40 / 16).
 * Retourne ESP_OK si login + fetch réussis + poêle trouvé. */
esp_err_t cloud_rest_fetch_stove_info(const char *username,
                                       const char *password,
                                       const char *my_matricola,
                                       char *out_stove_id, size_t sid_len,
                                       char *out_stove_model, size_t smod_len);

#endif
