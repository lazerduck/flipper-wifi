#include "modules/wifi/wifi_command.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_wifi.h"

#include "modules/wifi/wifi_discovery.h"
#include "modules/wifi/wifi_mdns.h"
#include "modules/wifi/wifi_manager.h"

#define WIFI_SUBCOMMAND_MAX_LENGTH 32
#define WIFI_STATUS_LINE_MAX_LENGTH 128
#define WIFI_SSID_MAX_LENGTH 33
#define WIFI_PASSWORD_MAX_LENGTH 65
#define WIFI_MDNS_HOST_MAX_LENGTH 96

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

static const char *skip_spaces(const char *cursor)
{
    while (cursor != NULL && *cursor == ' ') {
        ++cursor;
    }

    return cursor;
}

static bool copy_quoted_value(const char **cursor, char *value, size_t value_size)
{
    size_t value_length = 0;
    const char *read_cursor;

    if (cursor == NULL || *cursor == NULL || value == NULL || value_size == 0 || **cursor != '"') {
        return false;
    }

    read_cursor = *cursor + 1;
    while (*read_cursor != '\0') {
        char current = *read_cursor;

        if (current == '"') {
            if (value_length == 0) {
                return false;
            }

            value[value_length] = '\0';
            read_cursor++;
            if (*read_cursor != '\0' && *read_cursor != ' ') {
                return false;
            }

            *cursor = read_cursor;
            return true;
        }

        if (current == '\\') {
            ++read_cursor;
            if (*read_cursor == '\0') {
                return false;
            }

            current = *read_cursor;
        }

        if (value_length + 1U >= value_size) {
            return false;
        }

        value[value_length++] = current;
        ++read_cursor;
    }

    return false;
}

static bool copy_unquoted_value(const char **cursor, char *value, size_t value_size)
{
    size_t value_length = 0;
    const char *read_cursor;

    if (cursor == NULL || *cursor == NULL || value == NULL || value_size == 0) {
        return false;
    }

    read_cursor = *cursor;
    while (*read_cursor != '\0' && *read_cursor != ' ') {
        if (value_length + 1U >= value_size) {
            return false;
        }

        value[value_length++] = *read_cursor;
        ++read_cursor;
    }

    if (value_length == 0) {
        return false;
    }

    value[value_length] = '\0';
    *cursor = read_cursor;
    return true;
}

static bool read_named_arg(const char *args, const char *key, char *value, size_t value_size)
{
    const char *cursor = args;
    size_t key_length;

    if (args == NULL || key == NULL || value == NULL || value_size == 0) {
        return false;
    }

    key_length = strlen(key);
    value[0] = '\0';

    while (*cursor != '\0') {
        const char *key_end;

        cursor = skip_spaces(cursor);
        if (*cursor == '\0') {
            break;
        }

        key_end = strchr(cursor, '=');
        if (key_end == NULL) {
            return false;
        }

        if ((size_t)(key_end - cursor) == key_length && strncmp(cursor, key, key_length) == 0) {
            const char *value_cursor = key_end + 1;

            if (*value_cursor == '"') {
                return copy_quoted_value(&value_cursor, value, value_size);
            }

            return copy_unquoted_value(&value_cursor, value, value_size);
        }

        cursor = key_end + 1;
        if (*cursor == '"') {
            if (!copy_quoted_value(&cursor, value, value_size)) {
                return false;
            }
        } else {
            if (!copy_unquoted_value(&cursor, value, value_size)) {
                return false;
            }
        }

        cursor = skip_spaces(cursor);
    }

    value[0] = '\0';
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

static void write_discover_error(const command_context_t *context, esp_err_t err)
{
    if (err == ESP_ERR_NOT_SUPPORTED) {
        command_context_write_line(context, "ERR DISCOVER_SUBNET_TOO_LARGE\n");
        return;
    }

    if (err == ESP_ERR_INVALID_STATE) {
        command_context_write_line(context, "ERR DISCOVER_NOT_READY\n");
        return;
    }

    if (err == ESP_ERR_TIMEOUT) {
        command_context_write_line(context, "ERR DISCOVER_TIMEOUT\n");
        return;
    }

    if (err == WIFI_DISCOVERY_ERR_BUSY) {
        command_context_write_line(context, "ERR DISCOVER_BUSY\n");
        return;
    }

    if (err == WIFI_DISCOVERY_ERR_IP_INFO_FAILED) {
        command_context_write_line(context, "ERR DISCOVER_IP_INFO_FAILED\n");
        return;
    }

    if (err == ESP_ERR_NO_MEM) {
        command_context_write_line(context, "ERR DISCOVER_NO_MEM\n");
        return;
    }

    command_context_write_line(context, "ERR DISCOVER_FAILED\n");
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
        command_context_write_line(context, "ERR USAGE WIFI <SCAN|CONNECT|STATUS|READ_MDNS|DISCONNECT|DISCOVER>\n");
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

    if (strcmp(subcommand_name, "DISCOVER") == 0) {
        wifi_manager_status_t status;
        esp_err_t err;

        if (args[0] != '\0') {
            command_context_write_line(context, "ERR USAGE WIFI DISCOVER\n");
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

        err = wifi_discovery_scan_subnet(write_scan_result, &((wifi_scan_writer_context_t){
            .command_context = context,
        }));
        if (err != ESP_OK) {
            write_discover_error(context, err);
            return true;
        }

        return true;
    }

    if (strcmp(subcommand_name, "READ_MDNS") == 0) {
        wifi_manager_status_t status;
        char host[WIFI_MDNS_HOST_MAX_LENGTH];
        esp_err_t err;

        memset(host, 0, sizeof(host));

        if (!read_named_arg(args, "host", host, sizeof(host))) {
            command_context_write_line(context, "ERR USAGE WIFI READ_MDNS host=<hostname>\n");
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

        err = wifi_mdns_query_hostname(host, write_scan_result, &((wifi_scan_writer_context_t){
            .command_context = context,
        }));
        if (err == ESP_ERR_NOT_FOUND) {
            command_context_write_line(context, "ERR MDNS_NOT_FOUND\n");
            return true;
        }

        if (err != ESP_OK) {
            command_context_write_line(context, "ERR MDNS_QUERY_FAILED\n");
            return true;
        }

        return true;
    }

    command_context_write_line(context, "ERR UNKNOWN_WIFI_COMMAND\n");

    return true;
}