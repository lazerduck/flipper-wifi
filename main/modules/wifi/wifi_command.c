#include "modules/wifi/wifi_command.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_wifi.h"

#include "modules/wifi/wifi_manager.h"

#define WIFI_SUBCOMMAND_MAX_LENGTH 32
#define WIFI_STATUS_LINE_MAX_LENGTH 128
#define WIFI_SSID_MAX_LENGTH 33
#define WIFI_PASSWORD_MAX_LENGTH 65

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

static bool read_named_arg(const char *args, const char *key, char *value, size_t value_size)
{
    const char *cursor = args;
    size_t key_length;

    if (args == NULL || key == NULL || value == NULL || value_size == 0) {
        return false;
    }

    key_length = strlen(key);
    while (*cursor != '\0') {
        const char *token_end = strchr(cursor, ' ');
        size_t token_length = token_end == NULL ? strlen(cursor) : (size_t)(token_end - cursor);

        if (token_length > key_length + 1U && strncmp(cursor, key, key_length) == 0 && cursor[key_length] == '=') {
            size_t value_length = token_length - key_length - 1U;
            if (value_length >= value_size) {
                return false;
            }

            memcpy(value, cursor + key_length + 1U, value_length);
            value[value_length] = '\0';
            return true;
        }

        if (token_end == NULL) {
            break;
        }

        cursor = token_end + 1;
    }

    return false;
}

static void write_status_line(const command_context_t *context, const wifi_manager_status_t *status)
{
    char line[WIFI_STATUS_LINE_MAX_LENGTH];

    snprintf(
        line,
        sizeof(line),
        "WIFI STATUS state=%s connected=%s ssid=%s reason=%d\n",
        wifi_manager_state_to_string(status->state),
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
        command_context_write_line(context, "ERR USAGE WIFI <SCAN|CONNECT|STATUS|READ_MDNS|DISCONNECT>\n");
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

    if (strcmp(subcommand_name, "CONNECT") == 0) {
        char ssid[WIFI_SSID_MAX_LENGTH];
        char password[WIFI_PASSWORD_MAX_LENGTH];
        esp_err_t err;

        memset(ssid, 0, sizeof(ssid));
        memset(password, 0, sizeof(password));

        if (!read_named_arg(args, "ssid", ssid, sizeof(ssid)) || !read_named_arg(args, "psw", password, sizeof(password))) {
            command_context_write_line(context, "ERR USAGE WIFI CONNECT ssid=<ssid> psw=<password>\n");
            return true;
        }

        err = wifi_manager_connect(ssid, password);
        if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_CONNECT_FAILED\n");
            return true;
        }

        command_context_write_line(context, "WIFI CONNECTING\n");
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

    if (strcmp(subcommand_name, "DISCONNECT") == 0) {
        esp_err_t err;

        if (args[0] != '\0') {
            command_context_write_line(context, "ERR USAGE WIFI DISCONNECT\n");
            return true;
        }

        err = wifi_manager_disconnect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
            command_context_write_line(context, "ERR WIFI_DISCONNECT_FAILED\n");
            return true;
        }

        command_context_write_line(context, "WIFI DISCONNECTING\n");
        return true;
    }

    if (strcmp(subcommand_name, "READ_MDNS") == 0) {
        wifi_manager_status_t status;
        esp_err_t err;

        if (args[0] != '\0') {
            command_context_write_line(context, "ERR USAGE WIFI READ_MDNS\n");
            return true;
        }

        err = wifi_manager_refresh_status(&status);
        if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_STATUS_FAILED\n");
            return true;
        }

        if (status.state != WIFI_MANAGER_STATE_CONNECTED || !status.has_ip) {
            command_context_write_line(context, "ERR WIFI_NOT_CONNECTED\n");
            return true;
        }

        command_context_write_line(context, "ERR MDNS_NOT_IMPLEMENTED\n");
        return true;
    }

    command_context_write_line(context, "ERR UNKNOWN_WIFI_COMMAND\n");

    return true;
}