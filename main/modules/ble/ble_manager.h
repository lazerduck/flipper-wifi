#pragma once

#include "esp_err.h"

#include "modules/ble/ble_command.h"
#include "host/ble_gap.h"

#include <stdbool.h>
#include <stdint.h>

#define BLE_MANAGER_MAX_RESULTS 20

/* Callback fired for every raw advertisement during a streaming scan.
 * Called from the NimBLE host task — must be fast and thread-safe. */
typedef void (*ble_stream_cb_t)(const ble_addr_t *addr, int8_t rssi,
                                const uint8_t *adv_data, uint8_t adv_data_len,
                                void *context);

esp_err_t ble_manager_ensure_init(void);
bool ble_manager_try_acquire(void);
void ble_manager_release(void);

uint16_t ble_manager_default_scan_duration_ms(void);
uint16_t ble_manager_clamp_scan_duration_ms(uint32_t duration_ms);
esp_err_t ble_manager_scan(uint16_t duration_ms, ble_scan_result_writer_t write_line, void *context);

/* Continuous passive scan — fires on_adv for every advertisement received.
 * Runs until *stop becomes true (set from any task); then cancels the scan
 * and returns.  Holds the scan mutex for its entire duration. */
esp_err_t ble_manager_scan_stream(ble_stream_cb_t on_adv, void *context,
                                  const volatile bool *stop);
