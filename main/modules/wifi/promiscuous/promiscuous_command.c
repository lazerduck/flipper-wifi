#include "modules/wifi/promiscuous/promiscuous_command.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"

#include "modules/wifi/promiscuous/wifi_promiscuous.h"
#include "modules/wifi/wifi_manager.h"

#define WIFI_PROMISCUOUS_CHANNEL_LIST_MAX_LENGTH 64
#define WIFI_PROMISCUOUS_SUBCOMMAND_MAX_LENGTH 32

typedef struct {
    command_response_writer_t write_response;
} wifi_promiscuous_writer_context_t;

static wifi_promiscuous_writer_context_t s_watch_writer_context;

typedef enum {
    WIFI_READ_ARG_NOT_FOUND = 0,
    WIFI_READ_ARG_FOUND,
    WIFI_READ_ARG_INVALID,
} wifi_read_arg_result_t;

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

static wifi_read_arg_result_t read_named_arg(
    const char *args,
    const char *key,
    char *value,
    size_t value_size)
{
    const char *cursor = args;
    size_t key_length;

    if (args == NULL || key == NULL || value == NULL || value_size == 0) {
        return WIFI_READ_ARG_INVALID;
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
            return WIFI_READ_ARG_INVALID;
        }

        if ((size_t)(key_end - cursor) == key_length && strncmp(cursor, key, key_length) == 0) {
            const char *value_cursor = key_end + 1;
            bool ok;

            if (*value_cursor == '"') {
                ok = copy_quoted_value(&value_cursor, value, value_size);
            } else {
                ok = copy_unquoted_value(&value_cursor, value, value_size);
            }

            return ok ? WIFI_READ_ARG_FOUND : WIFI_READ_ARG_INVALID;
        }

        cursor = key_end + 1;
        if (*cursor == '"') {
            if (!copy_quoted_value(&cursor, value, value_size)) {
                return WIFI_READ_ARG_INVALID;
            }
        } else if (!copy_unquoted_value(&cursor, value, value_size)) {
            return WIFI_READ_ARG_INVALID;
        }

        cursor = skip_spaces(cursor);
    }

    return WIFI_READ_ARG_NOT_FOUND;
}

static bool parse_u32(const char *value, uint32_t *result)
{
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || result == NULL || value[0] == '\0') {
        return false;
    }

    parsed = strtoul(value, &end, 10);
    if (end == NULL || *end != '\0' || parsed > UINT_MAX) {
        return false;
    }

    *result = (uint32_t)parsed;
    return true;
}

static bool parse_i8(const char *value, int8_t *result)
{
    char *end = NULL;
    long parsed;

    if (value == NULL || result == NULL || value[0] == '\0') {
        return false;
    }

    parsed = strtol(value, &end, 10);
    if (end == NULL || *end != '\0' || parsed < -127L || parsed > 0L) {
        return false;
    }

    *result = (int8_t)parsed;
    return true;
}

static bool parse_channel_list(const char *value, uint8_t *channels, size_t *channel_count)
{
    char buffer[WIFI_PROMISCUOUS_CHANNEL_LIST_MAX_LENGTH];
    char *token;
    char *saveptr = NULL;
    size_t count = 0;

    if (value == NULL || channels == NULL || channel_count == NULL || value[0] == '\0') {
        return false;
    }

    if (strlen(value) >= sizeof(buffer)) {
        return false;
    }

    snprintf(buffer, sizeof(buffer), "%s", value);
    token = strtok_r(buffer, ",", &saveptr);
    while (token != NULL) {
        uint32_t channel;

        if (count >= WIFI_PROMISCUOUS_MAX_CHANNELS || !parse_u32(token, &channel) ||
            channel == 0U || channel > 14U) {
            return false;
        }

        channels[count++] = (uint8_t)channel;
        token = strtok_r(NULL, ",", &saveptr);
    }

    if (count == 0U) {
        return false;
    }

    *channel_count = count;
    return true;
}

static void write_result_line(const char *line, void *context)
{
    const wifi_promiscuous_writer_context_t *writer_context =
        (const wifi_promiscuous_writer_context_t *)context;

    if (writer_context == NULL || writer_context->write_response == NULL) {
        return;
    }

    writer_context->write_response(line);
}

static void write_operation_error(const command_context_t *context, const char *prefix, esp_err_t err)
{
    if (context == NULL || prefix == NULL) {
        return;
    }

    if (err == ESP_ERR_INVALID_ARG) {
        if (strcmp(prefix, "SURVEY") == 0) {
            command_context_write_line(
                context,
                "ERR USAGE WIFI PROMISCUOUS SURVEY [channels=<comma_list>] [dwell_ms=<ms>] [rssi_min=<dbm>]\n");
        } else if (strcmp(prefix, "WATCH") == 0) {
            command_context_write_line(
                context,
                "ERR USAGE WIFI PROMISCUOUS WATCH channel=<n> [interval_ms=<ms>] [rssi_min=<dbm>]\n");
        } else if (strcmp(prefix, "ENTER") == 0) {
            command_context_write_line(
                context,
                "ERR USAGE WIFI PROMISCUOUS ENTER [channel=<n>]\n");
        } else if (strcmp(prefix, "WATCH_STOP") == 0) {
            command_context_write_line(
                context,
                "ERR USAGE WIFI PROMISCUOUS WATCH_STOP\n");
        }
        return;
    }

    if (err == ESP_ERR_INVALID_STATE) {
        command_context_write_line(context, "ERR WIFI_MODE_CONFLICT\n");
        return;
    }

    if (err == WIFI_PROMISCUOUS_ERR_NOT_ACTIVE) {
        command_context_write_line(context, "ERR WIFI_NOT_PROMISCUOUS\n");
        return;
    }

    if (err == WIFI_PROMISCUOUS_ERR_BUSY) {
        command_context_write_line(context, "ERR WIFI_PROMISCUOUS_BUSY\n");
        return;
    }

    if (strcmp(prefix, "ENTER") == 0 || strcmp(prefix, "EXIT") == 0) {
        command_context_write_line(context, "ERR WIFI_PROMISCUOUS_FAILED\n");
    } else if (strcmp(prefix, "SURVEY") == 0) {
        command_context_write_line(context, "ERR WIFI_SURVEY_FAILED\n");
    } else if (strcmp(prefix, "WATCH") == 0) {
        command_context_write_line(context, "ERR WIFI_WATCH_FAILED\n");
    }
}

static void write_usage(const command_context_t *context)
{
    command_context_write_line(
        context,
    "ERR USAGE WIFI PROMISCUOUS <ENTER|EXIT|SURVEY|WATCH|WATCH_STOP>\n");
}

bool wifi_promiscuous_command_try_handle(const char *args, const command_context_t *context)
{
    const char *subcommand = args;
    const char *subcommand_args = "";
    char subcommand_name[WIFI_PROMISCUOUS_SUBCOMMAND_MAX_LENGTH];
    size_t subcommand_length = 0;
    wifi_manager_status_t status;
    esp_err_t err;

    if (args == NULL || context == NULL) {
        return false;
    }

    while (*subcommand == ' ') {
        ++subcommand;
    }

    if (*subcommand == '\0') {
        write_usage(context);
        return true;
    }

    subcommand_args = strchr(subcommand, ' ');
    if (subcommand_args == NULL) {
        subcommand_length = strlen(subcommand);
        subcommand_args = "";
    } else {
        subcommand_length = (size_t)(subcommand_args - subcommand);
        ++subcommand_args;
    }

    if (subcommand_length == 0 || subcommand_length >= sizeof(subcommand_name)) {
        write_usage(context);
        return true;
    }

    memcpy(subcommand_name, subcommand, subcommand_length);
    subcommand_name[subcommand_length] = '\0';

    err = wifi_manager_refresh_status(&status);
    if (err != ESP_OK) {
        command_context_write_line(context, "ERR WIFI_STATUS_FAILED\n");
        return true;
    }

    if (status.mode == WIFI_MANAGER_MODE_CONNECTED &&
        strcmp(subcommand_name, "EXIT") != 0) {
        command_context_write_line(context, "ERR WIFI_MODE_CONFLICT\n");
        return true;
    }

    if (strcmp(subcommand_name, "ENTER") == 0) {
        char channel_string[8];
        uint32_t channel = 1U;
        wifi_read_arg_result_t channel_result;

        memset(channel_string, 0, sizeof(channel_string));
        channel_result = read_named_arg(subcommand_args, "channel", channel_string, sizeof(channel_string));
        if (channel_result == WIFI_READ_ARG_INVALID ||
            (channel_result == WIFI_READ_ARG_FOUND && !parse_u32(channel_string, &channel))) {
            write_operation_error(context, "ENTER", ESP_ERR_INVALID_ARG);
            return true;
        }

        err = wifi_manager_enter_promiscuous((uint8_t)channel);
        if (err != ESP_OK) {
            write_operation_error(context, "ENTER", err);
            return true;
        }

        command_context_write_line(context, "WIFI PROMISCUOUS ENTERED\n");
        return true;
    }

    if (strcmp(subcommand_name, "EXIT") == 0) {
        if (subcommand_args[0] != '\0') {
            write_usage(context);
            return true;
        }

        err = wifi_promiscuous_watch_stop();
        if (err != ESP_OK) {
            write_operation_error(context, "WATCH", err);
            return true;
        }

        err = wifi_manager_exit_promiscuous();
        if (err != ESP_OK) {
            write_operation_error(context, "EXIT", err);
            return true;
        }

        command_context_write_line(context, "WIFI PROMISCUOUS EXITED\n");
        return true;
    }

    if (strcmp(subcommand_name, "SURVEY") == 0) {
        wifi_promiscuous_survey_config_t config = {
            .channel_count = 11U,
            .dwell_ms = 250U,
            .rssi_min = -95,
        };
        wifi_promiscuous_writer_context_t writer_context = {
            .write_response = context->write_response,
        };
        char channels_string[WIFI_PROMISCUOUS_CHANNEL_LIST_MAX_LENGTH];
        char dwell_string[16];
        char rssi_string[16];
        wifi_read_arg_result_t channels_result;
        wifi_read_arg_result_t dwell_result;
        wifi_read_arg_result_t rssi_result;

        memset(channels_string, 0, sizeof(channels_string));
        memset(dwell_string, 0, sizeof(dwell_string));
        memset(rssi_string, 0, sizeof(rssi_string));

        for (size_t index = 0; index < config.channel_count; ++index) {
            config.channels[index] = (uint8_t)(index + 1U);
        }

        channels_result = read_named_arg(subcommand_args, "channels", channels_string, sizeof(channels_string));
        if (channels_result == WIFI_READ_ARG_INVALID ||
            (channels_result == WIFI_READ_ARG_FOUND &&
             !parse_channel_list(channels_string, config.channels, &config.channel_count))) {
            write_operation_error(context, "SURVEY", ESP_ERR_INVALID_ARG);
            return true;
        }

        dwell_result = read_named_arg(subcommand_args, "dwell_ms", dwell_string, sizeof(dwell_string));
        if (dwell_result == WIFI_READ_ARG_INVALID ||
            (dwell_result == WIFI_READ_ARG_FOUND && !parse_u32(dwell_string, &config.dwell_ms))) {
            write_operation_error(context, "SURVEY", ESP_ERR_INVALID_ARG);
            return true;
        }

        rssi_result = read_named_arg(subcommand_args, "rssi_min", rssi_string, sizeof(rssi_string));
        if (rssi_result == WIFI_READ_ARG_INVALID ||
            (rssi_result == WIFI_READ_ARG_FOUND && !parse_i8(rssi_string, &config.rssi_min))) {
            write_operation_error(context, "SURVEY", ESP_ERR_INVALID_ARG);
            return true;
        }

        if (status.mode != WIFI_MANAGER_MODE_PROMISCUOUS) {
            err = wifi_manager_enter_promiscuous(config.channels[0]);
            if (err != ESP_OK) {
                write_operation_error(context, "ENTER", err);
                return true;
            }

            command_context_write_line(context, "WIFI PROMISCUOUS ENTERED\n");
        }

        err = wifi_manager_set_action(WIFI_MANAGER_ACTION_SURVEYING);
        if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_STATUS_FAILED\n");
            return true;
        }

        err = wifi_promiscuous_survey(&config, write_result_line, &writer_context);
        wifi_manager_clear_action();
        if (err != ESP_OK) {
            write_operation_error(context, "SURVEY", err);
            return true;
        }

        return true;
    }

    if (strcmp(subcommand_name, "WATCH") == 0) {
        wifi_promiscuous_watch_config_t config = {
            .report_interval_ms = WIFI_PROMISCUOUS_WATCH_REPORT_INTERVAL_MS,
            .rssi_min = -95,
        };
        char channel_string[8];
        char interval_string[16];
        char rssi_string[16];
        uint32_t channel;
        wifi_read_arg_result_t channel_result;
        wifi_read_arg_result_t interval_result;
        wifi_read_arg_result_t rssi_result;

        memset(channel_string, 0, sizeof(channel_string));
        memset(interval_string, 0, sizeof(interval_string));
        memset(rssi_string, 0, sizeof(rssi_string));

        channel_result = read_named_arg(subcommand_args, "channel", channel_string, sizeof(channel_string));
        if (channel_result != WIFI_READ_ARG_FOUND || !parse_u32(channel_string, &channel) ||
            channel == 0U || channel > 14U) {
            write_operation_error(context, "WATCH", ESP_ERR_INVALID_ARG);
            return true;
        }

        config.channel = (uint8_t)channel;

        interval_result = read_named_arg(subcommand_args, "interval_ms", interval_string, sizeof(interval_string));
        if (interval_result == WIFI_READ_ARG_INVALID ||
            (interval_result == WIFI_READ_ARG_FOUND && !parse_u32(interval_string, &config.report_interval_ms))) {
            write_operation_error(context, "WATCH", ESP_ERR_INVALID_ARG);
            return true;
        }

        rssi_result = read_named_arg(subcommand_args, "rssi_min", rssi_string, sizeof(rssi_string));
        if (rssi_result == WIFI_READ_ARG_INVALID ||
            (rssi_result == WIFI_READ_ARG_FOUND && !parse_i8(rssi_string, &config.rssi_min))) {
            write_operation_error(context, "WATCH", ESP_ERR_INVALID_ARG);
            return true;
        }

        if (status.mode != WIFI_MANAGER_MODE_PROMISCUOUS) {
            err = wifi_manager_enter_promiscuous(config.channel);
            if (err != ESP_OK) {
                write_operation_error(context, "ENTER", err);
                return true;
            }

            command_context_write_line(context, "WIFI PROMISCUOUS ENTERED\n");
        }

        err = wifi_manager_set_action(WIFI_MANAGER_ACTION_WATCHING);
        if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_STATUS_FAILED\n");
            return true;
        }

        s_watch_writer_context.write_response = context->write_response;

        err = wifi_promiscuous_watch_start(&config, write_result_line, &s_watch_writer_context);
        if (err != ESP_OK) {
            wifi_manager_clear_action();
            write_operation_error(context, "WATCH", err);
            return true;
        }

        return true;
    }

    if (strcmp(subcommand_name, "WATCH_STOP") == 0) {
        if (subcommand_args[0] != '\0') {
            write_operation_error(context, "WATCH_STOP", ESP_ERR_INVALID_ARG);
            return true;
        }

        err = wifi_promiscuous_watch_stop();
        if (err != ESP_OK) {
            write_operation_error(context, "WATCH", err);
            return true;
        }

        return true;
    }

    return false;
}
