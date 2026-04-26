#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "modules/wifi/wifi_manager.h"

typedef enum {
	STATUS_LED_BLE_STATE_IDLE,
	STATUS_LED_BLE_STATE_SCANNING,
	STATUS_LED_BLE_STATE_CONNECTING,
	STATUS_LED_BLE_STATE_GATT_ACTIVE,
} status_led_ble_state_t;

esp_err_t status_led_init(void);
bool status_led_is_ready(void);
esp_err_t status_led_apply_wifi_status(const wifi_manager_status_t *status);
esp_err_t status_led_set_ble_state(status_led_ble_state_t state);