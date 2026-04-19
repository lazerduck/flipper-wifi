#include "modules/wifi/beacon/beacon_command.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"

#include "modules/wifi/beacon/wifi_beacon.h"
#include "modules/wifi/wifi_manager.h"

#define BEACON_SUBCOMMAND_MAX_LENGTH    16U
#define BEACON_SSID_LIST_MAX_LENGTH     320U

/* Preset SSID pack — chosen to be amusing without being offensive.
   All are well-known harmless joke names used in the wild. */
static const char * const s_preset_ssids[] = {
    "FBI Surveillance Van",
    "Pretty Fly for a WiFi",
    "Not the FBI",
    "Loading...",
    "Abraham Linksys",
    "Bill Wi the Science Fi",
    "The LAN Before Time",
    "Silence of the LANs",
    "Wu-Tang LAN",
    "LAN Solo",
    "The Promised LAN",
    "Martin Router King",
    "Router? I Hardly Know Her",
    "Tell My WiFi Love Her",
    "Winternet Is Coming",
    "Drop It Like Its Hotspot",
    "Nacho WiFi",
    "404 Network Not Found",
    "Definitely Not Skynet",
    "It Hurts When IP",
};
static const size_t s_preset_ssid_count =
    sizeof(s_preset_ssids) / sizeof(s_preset_ssids[0]);

/* -----------------------------------------------------------------------
 * Arg parser (reuses the same pattern as promiscuous_command.c)
 * ----------------------------------------------------------------------- */

static const char *skip_spaces(const char *cursor)
{
    while (cursor != NULL && *cursor == ' ') {
        ++cursor;
    }
    return cursor;
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

typedef enum {
    ARG_NOT_FOUND = 0,
    ARG_FOUND,
    ARG_INVALID,
} arg_result_t;

static arg_result_t read_named_arg(
    const char *args,
    const char *key,
    char *value,
    size_t value_size)
{
    const char *cursor = args;
    size_t key_length;

    if (args == NULL || key == NULL || value == NULL || value_size == 0) {
        return ARG_INVALID;
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
            return ARG_INVALID;
        }

        if ((size_t)(key_end - cursor) == key_length &&
            strncmp(cursor, key, key_length) == 0) {
            const char *value_cursor = key_end + 1;
            bool ok = copy_unquoted_value(&value_cursor, value, value_size);
            return ok ? ARG_FOUND : ARG_INVALID;
        }

        /* Skip this key=value pair */
        cursor = key_end + 1;
        if (!copy_unquoted_value(&cursor, value, value_size)) {
            return ARG_INVALID;
        }

        cursor = skip_spaces(cursor);
    }

    return ARG_NOT_FOUND;
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

/* -----------------------------------------------------------------------
 * Result writer shim
 * ----------------------------------------------------------------------- */

typedef struct {
    command_response_writer_t write_response;
} beacon_writer_context_t;

static beacon_writer_context_t s_writer_context;

static void beacon_write_result_line(const char *line, void *context)
{
    const beacon_writer_context_t *ctx = (const beacon_writer_context_t *)context;
    if (ctx != NULL && ctx->write_response != NULL) {
        ctx->write_response(line);
    }
}

/* -----------------------------------------------------------------------
 * Public handler
 * ----------------------------------------------------------------------- */

bool wifi_beacon_command_try_handle(const char *args, const command_context_t *context)
{
    const char *subcommand = args;
    char subcommand_name[BEACON_SUBCOMMAND_MAX_LENGTH];
    size_t subcommand_length = 0;
    const char *subcommand_args = "";

    if (args == NULL || context == NULL) {
        return false;
    }

    while (*subcommand == ' ') {
        ++subcommand;
    }

    if (*subcommand == '\0') {
        command_context_write_line(
            context,
            "ERR USAGE WIFI BEACON <START|STOP> [channel=<1-13>] [duration_ms=<ms>]\n");
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

    if (subcommand_length == 0U || subcommand_length >= sizeof(subcommand_name)) {
        command_context_write_line(
            context,
            "ERR USAGE WIFI BEACON <START|STOP> [channel=<1-13>] [duration_ms=<ms>]\n");
        return true;
    }

    memcpy(subcommand_name, subcommand, subcommand_length);
    subcommand_name[subcommand_length] = '\0';

    /* ---- START ---- */
    if (strcmp(subcommand_name, "START") == 0) {
        wifi_beacon_config_t config = {
            .channel   = 6U,
            .duration_ms = 30000U,
        };
        char channel_str[8];
        char duration_str[12];
        arg_result_t channel_result;
        arg_result_t duration_result;
        esp_err_t err;

        memset(channel_str, 0, sizeof(channel_str));
        memset(duration_str, 0, sizeof(duration_str));

        channel_result = read_named_arg(subcommand_args, "channel", channel_str, sizeof(channel_str));
        if (channel_result == ARG_INVALID) {
            command_context_write_line(
                context,
                "ERR USAGE WIFI BEACON START [channel=<1-13>] [duration_ms=<ms>]\n");
            return true;
        }
        if (channel_result == ARG_FOUND) {
            uint32_t ch = 0U;
            if (!parse_u32(channel_str, &ch) || ch == 0U || ch > 13U) {
                command_context_write_line(
                    context,
                    "ERR USAGE WIFI BEACON START [channel=<1-13>] [duration_ms=<ms>]\n");
                return true;
            }
            config.channel = (uint8_t)ch;
        }

        duration_result = read_named_arg(subcommand_args, "duration_ms", duration_str, sizeof(duration_str));
        if (duration_result == ARG_INVALID) {
            command_context_write_line(
                context,
                "ERR USAGE WIFI BEACON START [channel=<1-13>] [duration_ms=<ms>]\n");
            return true;
        }
        if (duration_result == ARG_FOUND) {
            uint32_t dur = 0U;
            if (!parse_u32(duration_str, &dur) || dur == 0U || dur > WIFI_BEACON_MAX_DURATION_MS) {
                command_context_write_line(
                    context,
                    "ERR USAGE WIFI BEACON START [channel=<1-13>] [duration_ms=<ms>]\n");
                return true;
            }
            config.duration_ms = dur;
        }

        /* Load preset SSIDs into config */
        config.ssid_count = s_preset_ssid_count;
        for (size_t i = 0U; i < s_preset_ssid_count; ++i) {
            strncpy(
                config.ssids[i],
                s_preset_ssids[i],
                sizeof(config.ssids[i]) - 1U);
            config.ssids[i][sizeof(config.ssids[i]) - 1U] = '\0';
        }

        s_writer_context.write_response = context->write_response;

        err = wifi_beacon_start(&config, beacon_write_result_line, &s_writer_context);
        if (err == ESP_ERR_INVALID_ARG) {
            command_context_write_line(
                context,
                "ERR USAGE WIFI BEACON START [channel=<1-13>] [duration_ms=<ms>]\n");
        } else if (err == ESP_ERR_INVALID_STATE) {
            command_context_write_line(context, "ERR WIFI_MODE_CONFLICT\n");
        } else if (err == WIFI_BEACON_ERR_BUSY) {
            command_context_write_line(context, "ERR WIFI_BEACON_BUSY\n");
        } else if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_BEACON_FAILED\n");
        }
        /* On success, BEACON_STARTED is emitted by the task */
        return true;
    }

    /* ---- STOP ---- */
    if (strcmp(subcommand_name, "STOP") == 0) {
        esp_err_t err = wifi_beacon_stop();
        if (err == WIFI_BEACON_ERR_NOT_ACTIVE) {
            command_context_write_line(context, "ERR WIFI_BEACON_NOT_ACTIVE\n");
        } else if (err != ESP_OK) {
            command_context_write_line(context, "ERR WIFI_BEACON_FAILED\n");
        }
        /* On success, BEACON_DONE is emitted by the task */
        return true;
    }

    command_context_write_line(
        context,
        "ERR USAGE WIFI BEACON <START|STOP> [channel=<1-13>] [duration_ms=<ms>]\n");
    return true;
}
