#include "modules/ble/ble_command.h"

#include "esp_err.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modules/ble/ble_manager.h"
#include "modules/ble/ble_gatt.h"
#include "modules/status_led/status_led.h"

#define BLE_COMMAND_PREFIX "BLE"
#define BLE_SCAN_COMMAND "BLE SCAN"
#define BLE_SCAN_PREFIX "BLE SCAN "
#define BLE_STATUS_COMMAND "BLE STATUS"
#define BLE_GATT_PREFIX "BLE GATT"
#define BLE_GATT_FULL_PREFIX "BLE GATT "
#define BLE_GATT_MAC_KEY "mac="
#define BLE_GATT_ADDR_TYPE_KEY "addr_type="

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
        (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_SCANNING);
        esp_err_t err = ble_manager_scan(scan_duration_ms, ble_write_result_line, &writer_context);
        (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_IDLE);

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

    if (strcmp(command_line, BLE_GATT_PREFIX) == 0 ||
        strncmp(command_line, BLE_GATT_FULL_PREFIX, strlen(BLE_GATT_FULL_PREFIX)) == 0) {
        const char *args = NULL;
        char mac_buf[18] = {0};
        char addr_type_buf[16] = {0};
        const char *mac_ptr = NULL;
        const char *addr_type_ptr = NULL;

        if (strcmp(command_line, BLE_GATT_PREFIX) == 0) {
            command_context_write_line(context, "ERR USAGE BLE GATT mac=<MAC> [addr_type=<PUBLIC|RANDOM|PUBLIC_ID|RANDOM_ID>]\n");
            return true;
        }

        args = command_line + strlen(BLE_GATT_FULL_PREFIX);

        /* Parse mac= token */
        mac_ptr = strstr(args, BLE_GATT_MAC_KEY);
        if (mac_ptr != NULL) {
            mac_ptr += strlen(BLE_GATT_MAC_KEY);
            size_t i = 0;
            while (i < sizeof(mac_buf) - 1U && mac_ptr[i] != '\0' && mac_ptr[i] != ' ') {
                mac_buf[i] = mac_ptr[i];
                i++;
            }
            mac_buf[i] = '\0';
        }

        if (mac_buf[0] == '\0') {
            command_context_write_line(context, "ERR BLE_GATT_INVALID_ARGS\n");
            return true;
        }

        /* Parse optional addr_type= token */
        addr_type_ptr = strstr(args, BLE_GATT_ADDR_TYPE_KEY);
        if (addr_type_ptr != NULL) {
            addr_type_ptr += strlen(BLE_GATT_ADDR_TYPE_KEY);
            size_t i = 0;
            while (i < sizeof(addr_type_buf) - 1U &&
                   addr_type_ptr[i] != '\0' &&
                   addr_type_ptr[i] != ' ') {
                addr_type_buf[i] = addr_type_ptr[i];
                i++;
            }
            addr_type_buf[i] = '\0';
        }

        ble_writer_context_t writer_context = {
            .command_context = context,
        };

        (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_CONNECTING);

        esp_err_t err = ble_gatt_inspect(
            mac_buf,
            addr_type_buf[0] != '\0' ? addr_type_buf : NULL,
            ble_write_result_line,
            &writer_context);
        (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_IDLE);

        if (err == ESP_ERR_INVALID_STATE) {
            command_context_write_line(context, "ERR BLE_GATT_BUSY\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            command_context_write_line(context, "ERR BLE_GATT_INVALID_ARGS\n");
        } else if (err == ESP_ERR_TIMEOUT) {
            /* timeout message already emitted by ble_gatt_inspect */
        } else if (err != ESP_OK) {
            /* error message already emitted by ble_gatt_inspect */
        }
        return true;
    }

    if (strncmp(command_line, BLE_COMMAND_PREFIX " ", strlen(BLE_COMMAND_PREFIX " ")) == 0) {
        command_context_write_line(context, "ERR UNKNOWN_BLE_COMMAND\n");
        return true;
    }

    return false;
}