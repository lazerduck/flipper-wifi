#pragma once

#include "esp_err.h"

#include "modules/ble/ble_command.h"

#include <stdbool.h>
#include <stdint.h>

#define BLE_MANAGER_MAX_RESULTS 20

esp_err_t ble_manager_ensure_init(void);
bool ble_manager_try_acquire(void);
void ble_manager_release(void);

uint16_t ble_manager_default_scan_duration_ms(void);
uint16_t ble_manager_clamp_scan_duration_ms(uint32_t duration_ms);
esp_err_t ble_manager_scan(uint16_t duration_ms, ble_scan_result_writer_t write_line, void *context);
