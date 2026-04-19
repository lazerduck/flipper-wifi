#include "modules/ble/ble_command.h"

#include "esp_err.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modules/ble/ble_manager.h"

#define BLE_COMMAND_PREFIX "BLE"
#define BLE_SCAN_COMMAND "BLE SCAN"
#define BLE_SCAN_PREFIX "BLE SCAN "
#define BLE_STATUS_COMMAND "BLE STATUS"
#define BLE_GATT_PREFIX "BLE GATT"

typedef struct {
    const command_context_t *command_context;
} ble_writer_context_t;

static void ble_write_result_line(const char *line, void *context)
{
    const ble_writer_context_t *writer_context = (const ble_writer_context_t *)context;

    if (writer_context == NULL) {
        return;
    }

    command_context_write_line(writer_context->command_context, line);
}

bool ble_command_try_handle(const char *command_line, const command_context_t *context)
{
    if (command_line == NULL || context == NULL) {
        return false;
    }

    if (strcmp(command_line, BLE_COMMAND_PREFIX) == 0) {
        command_context_write_line(context, "ERR USAGE BLE <SCAN|STATUS|GATT>\n");
        return true;
    }

    if (strcmp(command_line, BLE_SCAN_COMMAND) == 0 ||
        strncmp(command_line, BLE_SCAN_PREFIX, strlen(BLE_SCAN_PREFIX)) == 0) {
        ble_writer_context_t writer_context = {
            .command_context = context,
        };
        char line[32];
        uint16_t scan_duration_ms = ble_manager_default_scan_duration_ms();

        if (strncmp(command_line, BLE_SCAN_PREFIX, strlen(BLE_SCAN_PREFIX)) == 0) {
            const char *value = command_line + strlen(BLE_SCAN_PREFIX);
            char *end = NULL;
            unsigned long parsed;

            if (value[0] == '\0') {
                command_context_write_line(context, "ERR USAGE BLE SCAN [duration_ms]\n");
                return true;
            }

            parsed = strtoul(value, &end, 10);
            if (end == value || *end != '\0') {
                command_context_write_line(context, "ERR USAGE BLE SCAN [duration_ms]\n");
                return true;
            }

            scan_duration_ms = ble_manager_clamp_scan_duration_ms(parsed);
        }

        snprintf(
            line,
            sizeof(line),
            "BLE_SCAN_START %u\n",
            (unsigned)scan_duration_ms);
        command_context_write_line(context, line);
        esp_err_t err = ble_manager_scan(scan_duration_ms, ble_write_result_line, &writer_context);

        if (err == ESP_ERR_INVALID_STATE) {
            command_context_write_line(context, "ERR BLE_SCAN_BUSY\n");
        } else if (err == ESP_ERR_TIMEOUT) {
            command_context_write_line(context, "ERR BLE_SCAN_TIMEOUT\n");
        } else if (err == ESP_ERR_NO_MEM) {
            command_context_write_line(context, "ERR BLE_SCAN_NO_MEM\n");
        } else if (err != ESP_OK) {
            command_context_write_line(context, "ERR BLE_SCAN_FAILED\n");
        }
        return true;
    }

    if (strcmp(command_line, BLE_STATUS_COMMAND) == 0) {
        command_context_write_line(context, "ERR BLE_STATUS_NOT_IMPLEMENTED\n");
        return true;
    }

    if (strncmp(command_line, BLE_GATT_PREFIX, strlen(BLE_GATT_PREFIX)) == 0) {
        command_context_write_line(context, "ERR BLE_GATT_NOT_IMPLEMENTED\n");
        return true;
    }

    if (strncmp(command_line, BLE_COMMAND_PREFIX " ", strlen(BLE_COMMAND_PREFIX " ")) == 0) {
        command_context_write_line(context, "ERR UNKNOWN_BLE_COMMAND\n");
        return true;
    }

    return false;
}