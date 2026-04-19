#pragma once

#include "esp_err.h"

#include "modules/ble/ble_command.h"

#include <stdint.h>

#define BLE_MANAGER_MAX_RESULTS 20

uint16_t ble_manager_default_scan_duration_ms(void);
uint16_t ble_manager_clamp_scan_duration_ms(uint32_t duration_ms);
esp_err_t ble_manager_scan(uint16_t duration_ms, ble_scan_result_writer_t write_line, void *context);