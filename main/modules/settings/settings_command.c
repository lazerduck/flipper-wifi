#include "modules/settings/settings_command.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "modules/settings/settings_manager.h"

#define SETTINGS_CMD_PREFIX     "CONFIG"
#define SETTINGS_CMD_PREFIX_LEN 6U
#define SETTINGS_KEY_BUF        48U
#define SETTINGS_VALUE_BUF      16U

/* ---------------------------------------------------------------------------
 * Minimal key=value arg parser (no quotes needed for bool values)
 * --------------------------------------------------------------------------- */

static const char* settings_cmd_skip_spaces(const char* p) {
    while(p != NULL && *p == ' ') {
        p++;
    }
    return p;
}

static bool settings_cmd_read_named(
    const char* args,
    const char* name,
    char*       out,
    size_t      out_size) {
    if(args == NULL || name == NULL || out == NULL || out_size == 0U) {
        return false;
    }

    size_t      name_len = strlen(name);
    const char* cursor   = args;

    while(*cursor != '\0') {
        cursor = settings_cmd_skip_spaces(cursor);
        if(*cursor == '\0') {
            break;
        }

        if(strncmp(cursor, name, name_len) == 0 && cursor[name_len] == '=') {
            const char* val = cursor + name_len + 1U;
            size_t      i   = 0U;
            while(*val != '\0' && *val != ' ' && i + 1U < out_size) {
                out[i++] = *val++;
            }
            out[i] = '\0';
            return i > 0U;
        }

        /* Skip to end of this token */
        while(*cursor != '\0' && *cursor != ' ') {
            cursor++;
        }
    }

    return false;
}

/* ---------------------------------------------------------------------------
 * CONFIG GET — emit all entries, SD status, then CONFIG_DONE
 * --------------------------------------------------------------------------- */

typedef struct {
    const command_context_t* ctx;
    char                     line[192];
} settings_emit_ctx_t;

static void settings_emit_entry_cb(
    const settings_descriptor_t* desc,
    const settings_values_t*     values,
    void*                        context) {
    settings_emit_ctx_t* ec       = (settings_emit_ctx_t*)context;
    const char*          type_str = (desc->type == SETTINGS_TYPE_BOOL) ? "bool" : "int";
    int                  val      = 0;

    /* Use the offset from the descriptor to read the field generically */
    if(desc->type == SETTINGS_TYPE_BOOL) {
        val = *((const bool*)((const uint8_t*)values + desc->offset)) ? 1 : 0;
    }

    snprintf(
        ec->line,
        sizeof(ec->line),
        "CONFIG_ENTRY key=%s type=%s value=%d label=\"%s\"\n",
        desc->key,
        type_str,
        val,
        desc->label);

    command_context_write_line(ec->ctx, ec->line);
}

static void settings_count_cb(
    const settings_descriptor_t* desc,
    const settings_values_t*     values,
    void*                        context) {
    (void)desc;
    (void)values;
    (*(size_t*)context)++;
}

static void settings_cmd_handle_get(const command_context_t* ctx) {
    char line[80];

    /* Count first so we can emit CONFIG_START count= */
    size_t total = 0U;
    settings_manager_iterate(settings_count_cb, &total);

    snprintf(line, sizeof(line), "CONFIG_START count=%u\n", (unsigned)total);
    command_context_write_line(ctx, line);

    /* Inform app whether SD is available */
    if(settings_manager_sd_available()) {
        command_context_write_line(ctx, "CONFIG_SD YES\n");
    } else {
        command_context_write_line(ctx, "CONFIG_SD NO\n");
    }

    /* Emit each entry */
    settings_emit_ctx_t ec;
    ec.ctx = ctx;
    settings_manager_iterate(settings_emit_entry_cb, &ec);

    command_context_write_line(ctx, "CONFIG_DONE\n");
}

/* ---------------------------------------------------------------------------
 * CONFIG SET key=<key> value=<0|1>
 * --------------------------------------------------------------------------- */

static void settings_cmd_handle_set(const char* args, const command_context_t* ctx) {
    char key[SETTINGS_KEY_BUF];
    char value[SETTINGS_VALUE_BUF];
    char line[96];

    if(!settings_cmd_read_named(args, "key", key, sizeof(key))) {
        command_context_write_line(ctx, "ERR USAGE CONFIG SET key=<key> value=<0|1>\n");
        return;
    }

    if(!settings_cmd_read_named(args, "value", value, sizeof(value))) {
        command_context_write_line(ctx, "ERR USAGE CONFIG SET key=<key> value=<0|1>\n");
        return;
    }

    if(strcmp(value, "0") != 0 && strcmp(value, "1") != 0) {
        command_context_write_line(ctx, "ERR CONFIG_INVALID_VALUE\n");
        return;
    }

    bool      bool_value = (value[0] == '1');
    esp_err_t err        = settings_manager_set_bool(key, bool_value);

    if(err == ESP_ERR_NOT_FOUND) {
        command_context_write_line(ctx, "ERR CONFIG_UNKNOWN_KEY\n");
        return;
    }

    if(err == ESP_ERR_INVALID_STATE) {
        /* In-memory was updated but SD is unavailable */
        command_context_write_line(ctx, "ERR CONFIG_NO_SD\n");
        return;
    }

    if(err != ESP_OK) {
        command_context_write_line(ctx, "ERR CONFIG_SD_WRITE_FAIL\n");
        return;
    }

    snprintf(line, sizeof(line), "CONFIG_SET_OK key=%s value=%s\n", key, value);
    command_context_write_line(ctx, line);
}

/* ---------------------------------------------------------------------------
 * CONFIG RESET
 * --------------------------------------------------------------------------- */

static void settings_cmd_handle_reset(const command_context_t* ctx) {
    esp_err_t err = settings_manager_reset();

    if(err == ESP_ERR_INVALID_STATE) {
        command_context_write_line(ctx, "ERR CONFIG_NO_SD\n");
        return;
    }

    if(err != ESP_OK) {
        command_context_write_line(ctx, "ERR CONFIG_SD_WRITE_FAIL\n");
        return;
    }

    command_context_write_line(ctx, "CONFIG_RESET_OK\n");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * --------------------------------------------------------------------------- */

bool settings_command_try_handle(const char* command_line, const command_context_t* context) {
    if(command_line == NULL || context == NULL) {
        return false;
    }

    if(strncmp(command_line, SETTINGS_CMD_PREFIX, SETTINGS_CMD_PREFIX_LEN) != 0) {
        return false;
    }

    const char* after = command_line + SETTINGS_CMD_PREFIX_LEN;

    if(*after == '\0') {
        command_context_write_line(context, "ERR USAGE CONFIG <GET|SET|RESET>\n");
        return true;
    }

    if(*after != ' ') {
        return false;
    }

    after++; /* skip space */

    if(strcmp(after, "GET") == 0) {
        settings_cmd_handle_get(context);
        return true;
    }

    if(strncmp(after, "SET ", 4) == 0) {
        settings_cmd_handle_set(after + 4, context);
        return true;
    }

    if(strcmp(after, "RESET") == 0) {
        settings_cmd_handle_reset(context);
        return true;
    }

    command_context_write_line(context, "ERR UNKNOWN_CONFIG_COMMAND\n");
    return true;
}
