#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "modules/wifi/wifi_manager.h"

esp_err_t status_led_init(void);
bool status_led_is_ready(void);
esp_err_t status_led_apply_wifi_status(const wifi_manager_status_t *status);