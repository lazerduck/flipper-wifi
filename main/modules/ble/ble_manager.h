#pragma once

#include "esp_err.h"

#include "modules/ble/ble_command.h"

#define BLE_MANAGER_MAX_RESULTS 20

esp_err_t ble_manager_scan(ble_scan_result_writer_t write_line, void *context);