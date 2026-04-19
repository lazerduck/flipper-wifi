#include "modules/ble/ble_command.h"

#include <string.h>

#define BLE_COMMAND_PREFIX "BLE"
#define BLE_SCAN_COMMAND "BLE SCAN"
#define BLE_STATUS_COMMAND "BLE STATUS"
#define BLE_GATT_PREFIX "BLE GATT"

bool ble_command_try_handle(const char *command_line, const command_context_t *context)
{
    if (command_line == NULL || context == NULL) {
        return false;
    }

    if (strcmp(command_line, BLE_COMMAND_PREFIX) == 0) {
        command_context_write_line(context, "ERR USAGE BLE <SCAN|STATUS|GATT>\n");
        return true;
    }

    if (strcmp(command_line, BLE_SCAN_COMMAND) == 0) {
        command_context_write_line(context, "ERR BLE_SCAN_NOT_IMPLEMENTED\n");
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