/**
 * openextraflame - Embedded HTTP server for config UI
 */

#pragma once

#include "esp_err.h"
#include "config_nvs.h"

esp_err_t web_ui_start(app_config_t *cfg);
