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

#define STATUS_LED_MAX_COMPONENT 16U

typedef struct {
	bool manual_override;
	uint8_t red;
	uint8_t green;
	uint8_t blue;
} status_led_state_t;

esp_err_t status_led_init(void);
bool status_led_is_ready(void);
esp_err_t status_led_apply_wifi_status(const wifi_manager_status_t *status);
esp_err_t status_led_set_ble_state(status_led_ble_state_t state);
esp_err_t status_led_set_manual_rgb(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t status_led_clear_manual_override(void);
esp_err_t status_led_get_state(status_led_state_t *state);