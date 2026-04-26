#pragma once

#include "esp_err.h"
#include "modules/ble/ble_command.h"

#include <stdint.h>

/*
 * BLE GATT inspect + read.
 *
 * Connects to a device, enumerates all services and characteristics, reads
 * the value of every READ-capable characteristic, then disconnects.
 *
 * All output is emitted via write_line in the following format:
 *
 *   BLE_GATT_START <mac>
 *   BLE_GATT_CONNECTED
 *   BLE_GATT_SVC <uuid> NAME <name|->
 *   BLE_GATT_CHR <svc_uuid> <chr_uuid> PROPS <props> NAME <name|->
 *   BLE_GATT_VAL <svc_uuid> <chr_uuid> <decoded_value>
 *   BLE_GATT_RAW <svc_uuid> <chr_uuid> <hex_payload>
 *   ...
 *   BLE_GATT_DONE
 *
 * On failure:
 *   BLE_GATT_CONNECT_FAILED
 *   BLE_GATT_DISCOVER_FAILED
 */

esp_err_t ble_gatt_inspect(
    const char *mac_str,
    const char *addr_type_str,
    ble_scan_result_writer_t write_line,
    void *context);
