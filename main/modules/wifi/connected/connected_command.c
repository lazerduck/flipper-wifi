#include "modules/wifi/connected/connected_command.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_wifi.h"

#include "modules/wifi/connected/wifi_discovery.h"
#include "modules/wifi/connected/wifi_http.h"
#include "modules/wifi/connected/wifi_mdns.h"
#include "modules/wifi/wifi_manager.h"

#define WIFI_HTTP_PRESET_MAX_LENGTH 32
#define WIFI_PASSWORD_MAX_LENGTH 65
#define WIFI_MDNS_HOST_MAX_LENGTH 96
#define WIFI_SSID_MAX_LENGTH 33

typedef struct {
    const command_context_t *command_context;
} wifi_connected_writer_context_t;

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
            value[value_length] = '\0';
            ++read_cursor;
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
        } else if (!copy_unquoted_value(&cursor, value, value_size)) {
            return false;
        }

        cursor = skip_spaces(cursor);
    }

    value[0] = '\0';
    return false;
}

static void write_result_line(const char *line, void *context)
{
    const wifi_connected_writer_context_t *writer_context =
        (const wifi_connected_writer_context_t *)context;

    if (writer_context == NULL) {
        return;
    }

    command_context_write_line(writer_context->command_context, line);
}

static bool wifi_connected_require_active_link(const command_context_t *context)
{
    wifi_manager_status_t status;
    esp_err_t err = wifi_manager_refresh_status(&status);

    if (err != ESP_OK) {
        command_context_write_line(context, "ERR WIFI_STATUS_FAILED\n");
        return false;
    }

    if (status.mode != WIFI_MANAGER_MODE_CONNECTED || !status.has_ip) {
        command_context_write_line(context, "ERR WIFI_NOT_CONNECTED\n");
        return false;
    }

    return true;
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

bool wifi_connected_command_try_handle(
    const char *subcommand_name,
    const char *args,
    const command_context_t *context)
{
    wifi_connected_writer_context_t writer_context = {
        .command_context = context,
    };

    if (subcommand_name == NULL || args == NULL || context == NULL) {
        return false;
    }

    if (strcmp(subcommand_name, "CONNECT") == 0) {
        char ssid[WIFI_SSID_MAX_LENGTH];
        char password[WIFI_PASSWORD_MAX_LENGTH];
        bool has_password;
        esp_err_t err;

        memset(ssid, 0, sizeof(ssid));
        memset(password, 0, sizeof(password));

        if (!read_named_arg(args, "ssid", ssid, sizeof(ssid))) {
            command_context_write_line(context, "ERR USAGE WIFI CONNECT ssid=<ssid> psw=<password>\n");
            return true;
        }

        has_password = read_named_arg(args, "psw", password, sizeof(password));
        if (!has_password) {
            password[0] = '\0';
        }

        err = wifi_manager_connect(ssid, password);
        if (err == ESP_ERR_INVALID_STATE) {
            command_context_write_line(context, "ERR WIFI_MODE_CONFLICT\n");
            return true;
        }

        if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_CONNECT_FAILED\n");
            return true;
        }

        command_context_write_line(context, "WIFI CONNECTING\n");
        return true;
    }

    if (strcmp(subcommand_name, "DISCONNECT") == 0) {
        esp_err_t err;

        if (args[0] != '\0') {
            command_context_write_line(context, "ERR USAGE WIFI DISCONNECT\n");
            return true;
        }

        err = wifi_manager_disconnect();
        if (err == ESP_ERR_INVALID_STATE) {
            command_context_write_line(context, "ERR WIFI_MODE_CONFLICT\n");
            return true;
        }

        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
            command_context_write_line(context, "ERR WIFI_DISCONNECT_FAILED\n");
            return true;
        }

        command_context_write_line(context, "WIFI DISCONNECTING\n");
        return true;
    }

    if (strcmp(subcommand_name, "DISCOVER") == 0) {
        esp_err_t err;

        if (args[0] != '\0') {
            command_context_write_line(context, "ERR USAGE WIFI DISCOVER\n");
            return true;
        }

        if (!wifi_connected_require_active_link(context)) {
            return true;
        }

        err = wifi_manager_set_action(WIFI_MANAGER_ACTION_DISCOVERING);
        if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_STATUS_FAILED\n");
            return true;
        }

        err = wifi_discovery_scan_subnet(write_result_line, &writer_context);
        wifi_manager_clear_action();
        if (err != ESP_OK) {
            write_discover_error(context, err);
            return true;
        }

        return true;
    }

    if (strcmp(subcommand_name, "HTTP") == 0) {
        char preset[WIFI_HTTP_PRESET_MAX_LENGTH];
        esp_err_t err;

        memset(preset, 0, sizeof(preset));

        if (!read_named_arg(args, "preset", preset, sizeof(preset))) {
            command_context_write_line(context, "ERR USAGE WIFI HTTP preset=<ip|time|location>\n");
            return true;
        }

        if (!wifi_connected_require_active_link(context)) {
            return true;
        }

        err = wifi_manager_set_action(WIFI_MANAGER_ACTION_REQUESTING_HTTP);
        if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_STATUS_FAILED\n");
            return true;
        }

        err = wifi_http_fetch_preset(preset, write_result_line, &writer_context);
        wifi_manager_clear_action();
        if (err == ESP_ERR_INVALID_ARG) {
            command_context_write_line(context, "ERR HTTP_UNKNOWN_PRESET\n");
            return true;
        }

        if (err == ESP_ERR_NO_MEM) {
            command_context_write_line(context, "ERR HTTP_NO_MEM\n");
            return true;
        }

        if (err == ESP_ERR_TIMEOUT) {
            command_context_write_line(context, "ERR HTTP_TIMEOUT\n");
            return true;
        }

        if (err != ESP_OK) {
            command_context_write_line(context, "ERR HTTP_FETCH_FAILED\n");
            return true;
        }

        return true;
    }

    if (strcmp(subcommand_name, "READ_MDNS") == 0) {
        char host[WIFI_MDNS_HOST_MAX_LENGTH];
        esp_err_t err;

        memset(host, 0, sizeof(host));

        if (!read_named_arg(args, "host", host, sizeof(host))) {
            command_context_write_line(context, "ERR USAGE WIFI READ_MDNS host=<hostname>\n");
            return true;
        }

        if (!wifi_connected_require_active_link(context)) {
            return true;
        }

        err = wifi_manager_set_action(WIFI_MANAGER_ACTION_RESOLVING_MDNS);
        if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_STATUS_FAILED\n");
            return true;
        }

        err = wifi_mdns_query_hostname(host, write_result_line, &writer_context);
        wifi_manager_clear_action();
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

    return false;
}
