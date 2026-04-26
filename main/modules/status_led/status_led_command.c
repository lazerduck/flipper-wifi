#include "modules/status_led/status_led_command.h"

#include <stdio.h>
#include <string.h>

#include "modules/status_led/status_led.h"

#define STATUS_LED_COMMAND_PREFIX "LED"

static bool status_led_command_matches_prefix(const char *command_line)
{
    if (strncmp(command_line, STATUS_LED_COMMAND_PREFIX, strlen(STATUS_LED_COMMAND_PREFIX)) != 0) {
        return false;
    }

    return command_line[3] == '\0' || command_line[3] == ' ';
}

static void status_led_command_write_status(const command_context_t *context)
{
    status_led_state_t state;
    char line[96];

    if (status_led_get_state(&state) != ESP_OK) {
        command_context_write_line(context, "ERR LED_STATUS_FAILED\n");
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "LED STATUS mode=%s red=%u green=%u blue=%u\n",
        state.manual_override ? "MANUAL" : "AUTO",
        (unsigned int)state.red,
        (unsigned int)state.green,
        (unsigned int)state.blue);
    command_context_write_line(context, line);
}

bool status_led_command_try_handle(const char *command_line, const command_context_t *context)
{
    unsigned int red = 0U;
    unsigned int green = 0U;
    unsigned int blue = 0U;
    char extra = '\0';

    if (command_line == NULL || context == NULL) {
        return false;
    }

    if (!status_led_command_matches_prefix(command_line)) {
        return false;
    }

    if (strcmp(command_line, "LED") == 0) {
        command_context_write_line(context, "ERR USAGE LED <STATUS|SET|AUTO>\n");
        return true;
    }

    if (strcmp(command_line, "LED STATUS") == 0) {
        status_led_command_write_status(context);
        return true;
    }

    if (strcmp(command_line, "LED AUTO") == 0) {
        if (status_led_clear_manual_override() != ESP_OK) {
            command_context_write_line(context, "ERR LED_AUTO_FAILED\n");
            return true;
        }

        status_led_command_write_status(context);
        return true;
    }

    if (strncmp(command_line, "LED SET ", 8) == 0) {
        if (sscanf(
                command_line + 8,
                "red=%u green=%u blue=%u %c",
                &red,
                &green,
                &blue,
                &extra) != 3) {
            command_context_write_line(
                context, "ERR USAGE LED SET red=<0-16> green=<0-16> blue=<0-16>\n");
            return true;
        }

        if (red > STATUS_LED_MAX_COMPONENT || green > STATUS_LED_MAX_COMPONENT ||
            blue > STATUS_LED_MAX_COMPONENT) {
            command_context_write_line(context, "ERR LED_INVALID_VALUE\n");
            return true;
        }

        if (status_led_set_manual_rgb((uint8_t)red, (uint8_t)green, (uint8_t)blue) != ESP_OK) {
            command_context_write_line(context, "ERR LED_SET_FAILED\n");
            return true;
        }

        status_led_command_write_status(context);
        return true;
    }

    command_context_write_line(context, "ERR UNKNOWN_LED_COMMAND\n");
    return true;
}