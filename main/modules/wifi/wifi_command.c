#include "modules/wifi/wifi_command.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"

#include "modules/wifi/connected/connected_command.h"
#include "modules/wifi/promiscuous/promiscuous_command.h"
#include "modules/wifi/wifi_manager.h"

#define WIFI_SUBCOMMAND_MAX_LENGTH 32
#define WIFI_STATUS_LINE_MAX_LENGTH 192

typedef struct {
    const command_context_t *command_context;
} wifi_scan_writer_context_t;

static bool wifi_command_matches_prefix(const char *command_line)
{
    if (strncmp(command_line, "WIFI", strlen("WIFI")) != 0) {
        return false;
    }

    return command_line[4] == '\0' || command_line[4] == ' ';
}

static void write_status_line(const command_context_t *context, const wifi_manager_status_t *status)
{
    char line[WIFI_STATUS_LINE_MAX_LENGTH];

    snprintf(
        line,
        sizeof(line),
        "WIFI STATUS mode=%s action=%s state=%s connected=%s ssid=%s reason=%d\n",
        wifi_manager_mode_to_string(status->mode),
        wifi_manager_action_to_string(status->action),
        wifi_manager_state_to_string(status->mode, status->action),
        status->has_ip ? "yes" : "no",
        status->ssid[0] == '\0' ? "-" : status->ssid,
        status->last_disconnect_reason);
    command_context_write_line(context, line);
}

static void write_scan_result(const char *line, void *context)
{
    wifi_scan_writer_context_t *scan_context = (wifi_scan_writer_context_t *)context;
    if (scan_context == NULL) {
        return;
    }

    command_context_write_line(scan_context->command_context, line);
}

bool wifi_command_try_handle(const char *command_line, const command_context_t *context)
{
    const char *subcommand = NULL;
    const char *args = "";
    char subcommand_name[WIFI_SUBCOMMAND_MAX_LENGTH];
    size_t subcommand_length = 0;

    if (command_line == NULL || context == NULL) {
        return false;
    }

    if (!wifi_command_matches_prefix(command_line)) {
        return false;
    }

    if (command_line[4] == '\0') {
        command_context_write_line(context, "ERR USAGE WIFI <SCAN|STATUS|CONNECT|DISCONNECT|DISCOVER|READ_MDNS|PROMISCUOUS>\n");
        return true;
    }

    subcommand = command_line + 5;
    args = strchr(subcommand, ' ');
    if (args == NULL) {
        subcommand_length = strlen(subcommand);
        args = "";
    } else {
        subcommand_length = (size_t)(args - subcommand);
        ++args;
    }

    if (subcommand_length == 0 || subcommand_length >= sizeof(subcommand_name)) {
        command_context_write_line(context, "ERR UNKNOWN_WIFI_COMMAND\n");
        return true;
    }

    memcpy(subcommand_name, subcommand, subcommand_length);
    subcommand_name[subcommand_length] = '\0';

    if (strcmp(subcommand_name, "SCAN") == 0) {
        wifi_scan_writer_context_t scan_context = {
            .command_context = context,
        };
        esp_err_t err;

        if (args[0] != '\0') {
            command_context_write_line(context, "ERR USAGE WIFI SCAN\n");
            return true;
        }

        err = wifi_manager_scan_aps(write_scan_result, &scan_context);
        if (err != ESP_OK) {
            command_context_write_line(context, "ERR SCAN_FAILED\n");
        }

        return true;
    }

    if (strcmp(subcommand_name, "STATUS") == 0) {
        wifi_manager_status_t status;
        esp_err_t err;

        if (args[0] != '\0') {
            command_context_write_line(context, "ERR USAGE WIFI STATUS\n");
            return true;
        }

        err = wifi_manager_refresh_status(&status);
        if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_STATUS_FAILED\n");
            return true;
        }

        write_status_line(context, &status);
        return true;
    }

    if (strcmp(subcommand_name, "PROMISCUOUS") == 0 || strcmp(subcommand_name, "PROMISC") == 0) {
        if (!wifi_promiscuous_command_try_handle(args, context)) {
            command_context_write_line(context, "ERR UNKNOWN_WIFI_COMMAND\n");
        }

        return true;
    }

    if (wifi_connected_command_try_handle(subcommand_name, args, context)) {
        return true;
    }

    command_context_write_line(context, "ERR UNKNOWN_WIFI_COMMAND\n");

    return true;
}