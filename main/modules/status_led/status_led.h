#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "modules/wifi/wifi_manager.h"

esp_err_t status_led_init(void);
bool status_led_is_ready(void);
esp_err_t status_led_apply_wifi_state(wifi_manager_state_t state);