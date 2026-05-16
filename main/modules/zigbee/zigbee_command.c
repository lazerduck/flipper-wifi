#include "modules/zigbee/zigbee_command.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"

#include "modules/system/system_mode_guard.h"
#include "modules/zigbee/zigbee_manager.h"
#include "modules/zigbee/zigbee_stack.h"

#define ZIGBEE_SUBCOMMAND_MAX 24U
#define ZIGBEE_VALUE_MAX 64U

typedef struct {
    const command_context_t* context;
    char line[196];
} zigbee_writer_t;

static bool zigbee_command_matches_prefix(const char* command_line) {
    if(strncmp(command_line, "ZIGBEE", 6) != 0) {
        return false;
    }

    return command_line[6] == '\0' || command_line[6] == ' ';
}

static const char* zigbee_skip_spaces(const char* p) {
    while(p != NULL && *p == ' ') {
        p++;
    }
    return p;
}

static bool zigbee_read_token(const char** cursor, char* out, size_t out_size) {
    const char* p;
    size_t i = 0U;

    if(cursor == NULL || *cursor == NULL || out == NULL || out_size == 0U) {
        return false;
    }

    p = zigbee_skip_spaces(*cursor);
    if(p == NULL || *p == '\0') {
        return false;
    }

    while(*p != '\0' && *p != ' ' && i + 1U < out_size) {
        out[i++] = *p++;
    }
    out[i] = '\0';

    while(*p != '\0' && *p != ' ') {
        p++;
    }

    *cursor = p;
    return i > 0U;
}

static bool zigbee_read_named_value(const char* args, const char* key, char* out, size_t out_size) {
    size_t key_len;
    const char* cursor;

    if(args == NULL || key == NULL || out == NULL || out_size == 0U) {
        return false;
    }

    key_len = strlen(key);
    cursor = args;

    while((cursor = strstr(cursor, key)) != NULL) {
        const char* value_start;
        size_t value_len = 0U;

        if(cursor != args && cursor[-1] != ' ') {
            cursor += key_len;
            continue;
        }

        if(cursor[key_len] != '=') {
            cursor += key_len;
            continue;
        }

        value_start = cursor + key_len + 1U;
        if(value_start[0] == '\0') {
            return false;
        }

        if(value_start[0] == '"') {
            value_start++;
            while(value_start[value_len] != '\0' && value_start[value_len] != '"') {
                value_len++;
            }
        } else {
            while(value_start[value_len] != '\0' && value_start[value_len] != ' ') {
                value_len++;
            }
        }

        if(value_len == 0U || value_len >= out_size) {
            return false;
        }

        memcpy(out, value_start, value_len);
        out[value_len] = '\0';
        return true;
    }

    return false;
}

static bool zigbee_parse_u32(const char* value, uint32_t* out) {
    char* end = NULL;
    unsigned long parsed;

    if(value == NULL || out == NULL || value[0] == '\0') {
        return false;
    }

    parsed = strtoul(value, &end, 10);
    if(end == NULL || *end != '\0') {
        return false;
    }

    *out = (uint32_t)parsed;
    return true;
}

static void zigbee_write_error(zigbee_writer_t* w, esp_err_t err) {
    if(err == ESP_ERR_NOT_FOUND) {
        command_context_write_line(w->context, "ERR ZIGBEE_PROFILE_NOT_FOUND\n");
        return;
    }

    if(err == ESP_ERR_INVALID_STATE) {
        command_context_write_line(w->context, "ERR ZIGBEE_INVALID_STATE\n");
        return;
    }

    if(err == ESP_ERR_NO_MEM) {
        command_context_write_line(w->context, "ERR ZIGBEE_NO_MEM\n");
        return;
    }

    if(err == ESP_ERR_INVALID_ARG) {
        command_context_write_line(w->context, "ERR ZIGBEE_INVALID_ARG\n");
        return;
    }

    command_context_write_line(w->context, "ERR ZIGBEE_OP_FAILED\n");
}

static void zigbee_handle_status(zigbee_writer_t* w, const char* args) {
    zigbee_manager_status_t status;
    esp_err_t err;

    if(args[0] != '\0') {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE STATUS\n");
        return;
    }

    err = zigbee_manager_get_status(&status);
    if(err != ESP_OK) {
        zigbee_write_error(w, err);
        return;
    }

    snprintf(
        w->line,
        sizeof(w->line),
        "ZIGBEE_STATUS joined=%s active_profile=%u pan_id=%u channel=%u tx_counter=%u profiles=%u sd=%s\n",
        status.joined ? "yes" : "no",
        (unsigned)status.active_profile_id,
        (unsigned)status.active_pan_id,
        (unsigned)status.active_channel,
        (unsigned)status.tx_frame_counter,
        (unsigned)status.profile_count,
        status.sd_available ? "yes" : "no");
    command_context_write_line(w->context, w->line);
}

static void zigbee_handle_list(zigbee_writer_t* w, const char* args) {
    const zigbee_profile_t* profiles = NULL;
    size_t count = 0U;
    size_t i;
    size_t button;
    esp_err_t err;

    if(args[0] != '\0') {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE LIST\n");
        return;
    }

    err = zigbee_manager_list_profiles(&profiles, &count);
    if(err != ESP_OK) {
        zigbee_write_error(w, err);
        return;
    }

    snprintf(w->line, sizeof(w->line), "ZIGBEE_LIST_START count=%u\n", (unsigned)count);
    command_context_write_line(w->context, w->line);

    for(i = 0U; i < count; i++) {
        const zigbee_profile_t* p = &profiles[i];
        snprintf(
            w->line,
            sizeof(w->line),
            "ZIGBEE_PROFILE id=%u name=\"%s\" joined=%u pan_id=%u channel=%u\n",
            (unsigned)p->id,
            p->name,
            p->joined ? 1U : 0U,
            (unsigned)p->pan_id,
            (unsigned)p->channel);
        command_context_write_line(w->context, w->line);

        for(button = 0U; button < ZIGBEE_BUTTONS_PER_PROFILE; button++) {
            snprintf(
                w->line,
                sizeof(w->line),
                "ZIGBEE_BUTTON profile_id=%u index=%u name=\"%s\" press=single\n",
                (unsigned)p->id,
                (unsigned)(button + 1U),
                p->buttons[button].name);
            command_context_write_line(w->context, w->line);
        }
    }

    command_context_write_line(w->context, "ZIGBEE_LIST_DONE\n");
}

static void zigbee_handle_profile_create(zigbee_writer_t* w, const char* args) {
    char name[ZIGBEE_VALUE_MAX];
    uint32_t profile_id;
    esp_err_t err;

    if(!zigbee_read_named_value(args, "name", name, sizeof(name))) {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE PROFILE CREATE name=<value>\n");
        return;
    }

    err = zigbee_manager_create_profile(name, &profile_id);
    if(err != ESP_OK) {
        zigbee_write_error(w, err);
        return;
    }

    snprintf(w->line, sizeof(w->line), "ZIGBEE_PROFILE_CREATED id=%u\n", (unsigned)profile_id);
    command_context_write_line(w->context, w->line);
}

static void zigbee_handle_profile_rename(zigbee_writer_t* w, const char* args) {
    char id_raw[ZIGBEE_VALUE_MAX];
    char name[ZIGBEE_VALUE_MAX];
    uint32_t profile_id;
    esp_err_t err;

    if(!zigbee_read_named_value(args, "id", id_raw, sizeof(id_raw)) ||
       !zigbee_parse_u32(id_raw, &profile_id) ||
       !zigbee_read_named_value(args, "name", name, sizeof(name))) {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE PROFILE RENAME id=<id> name=<value>\n");
        return;
    }

    err = zigbee_manager_rename_profile(profile_id, name);
    if(err != ESP_OK) {
        zigbee_write_error(w, err);
        return;
    }

    command_context_write_line(w->context, "ZIGBEE_PROFILE_RENAMED\n");
}

static void zigbee_handle_profile_delete(zigbee_writer_t* w, const char* args) {
    char id_raw[ZIGBEE_VALUE_MAX];
    uint32_t profile_id;
    esp_err_t err;

    if(!zigbee_read_named_value(args, "id", id_raw, sizeof(id_raw)) || !zigbee_parse_u32(id_raw, &profile_id)) {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE PROFILE DELETE id=<id>\n");
        return;
    }

    err = zigbee_manager_delete_profile(profile_id);
    if(err != ESP_OK) {
        zigbee_write_error(w, err);
        return;
    }

    command_context_write_line(w->context, "ZIGBEE_PROFILE_DELETED\n");
}

static void zigbee_handle_button_rename(zigbee_writer_t* w, const char* args) {
    char id_raw[ZIGBEE_VALUE_MAX];
    char index_raw[ZIGBEE_VALUE_MAX];
    char name[ZIGBEE_VALUE_MAX];
    uint32_t profile_id;
    uint32_t index;
    esp_err_t err;

    if(!zigbee_read_named_value(args, "id", id_raw, sizeof(id_raw)) ||
       !zigbee_parse_u32(id_raw, &profile_id) ||
       !zigbee_read_named_value(args, "index", index_raw, sizeof(index_raw)) ||
       !zigbee_parse_u32(index_raw, &index) ||
       index == 0U || index > ZIGBEE_BUTTONS_PER_PROFILE ||
       !zigbee_read_named_value(args, "name", name, sizeof(name))) {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE BUTTON RENAME id=<id> index=<1-8> name=<value>\n");
        return;
    }

    err = zigbee_manager_rename_button(profile_id, (uint8_t)(index - 1U), name);
    if(err != ESP_OK) {
        zigbee_write_error(w, err);
        return;
    }

    command_context_write_line(w->context, "ZIGBEE_BUTTON_RENAMED\n");
}

static void zigbee_handle_scan(zigbee_writer_t* w, const char* args) {
    char channel_raw[ZIGBEE_VALUE_MAX];
    uint32_t channel = 0U;
    zigbee_stack_scan_result_t result;
    esp_err_t err;

    /* optional: channel=0|11-26, default 0 = all channels */
    if(zigbee_read_named_value(args, "channel", channel_raw, sizeof(channel_raw))) {
        if(!zigbee_parse_u32(channel_raw, &channel) || channel > 26U) {
            command_context_write_line(w->context, "ERR USAGE ZIGBEE SCAN [channel=<0|11-26>]\n");
            return;
        }
    }

    err = zigbee_stack_scan((uint8_t)channel, &result);

    if(err == ESP_ERR_TIMEOUT) {
        command_context_write_line(w->context, "ZIGBEE_SCAN_RESULT status=TIMEOUT radio=possibly_faulty\n");
        return;
    }

    if(err == ESP_ERR_INVALID_STATE) {
        command_context_write_line(w->context, "ERR ZIGBEE_INVALID_STATE\n");
        return;
    }

    if(err != ESP_OK) {
        command_context_write_line(w->context, "ERR ZIGBEE_OP_FAILED\n");
        return;
    }

    if(result.joined) {
        snprintf(
            w->line,
            sizeof(w->line),
            "ZIGBEE_SCAN_RESULT status=OPEN_NETWORK_FOUND radio=ok"
            " pan=0x%04x channel=%u short=0x%04x\n",
            (unsigned)result.net.pan_id,
            (unsigned)result.net.channel,
            (unsigned)result.net.short_addr);
    } else if(result.bdb_status == ZIGBEE_BDB_STATUS_NO_NETWORK) {
        snprintf(
            w->line,
            sizeof(w->line),
            "ZIGBEE_SCAN_RESULT status=NO_OPEN_NETWORK radio=ok\n");
    } else {
        snprintf(
            w->line,
            sizeof(w->line),
            "ZIGBEE_SCAN_RESULT status=BDB_ERROR radio=ok bdb_status=0x%02x\n",
            (unsigned)result.bdb_status);
    }
    command_context_write_line(w->context, w->line);
}

static void zigbee_handle_join(zigbee_writer_t* w, const char* args) {
    char id_raw[ZIGBEE_VALUE_MAX];
    char pan_raw[ZIGBEE_VALUE_MAX];
    char channel_raw[ZIGBEE_VALUE_MAX];
    uint32_t profile_id;
    uint32_t pan_id;
    uint32_t channel;
    esp_err_t err;

    if(!zigbee_read_named_value(args, "id", id_raw, sizeof(id_raw)) ||
       !zigbee_parse_u32(id_raw, &profile_id) ||
       !zigbee_read_named_value(args, "pan", pan_raw, sizeof(pan_raw)) ||
       !zigbee_parse_u32(pan_raw, &pan_id) ||
       pan_id > 0xFFFFU ||
       !zigbee_read_named_value(args, "channel", channel_raw, sizeof(channel_raw)) ||
       !zigbee_parse_u32(channel_raw, &channel) ||
       channel > 0xFFU) {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE JOIN id=<id> pan=<0-65535> channel=<0|11-26>\n");
        return;
    }

    err = zigbee_manager_join_profile(profile_id, (uint16_t)pan_id, (uint8_t)channel);
    if(err != ESP_OK) {
        zigbee_write_error(w, err);
        return;
    }

    command_context_write_line(w->context, "ZIGBEE_JOINED\n");
}

static void zigbee_handle_leave(zigbee_writer_t* w, const char* args) {
    esp_err_t err;

    if(args[0] != '\0') {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE LEAVE\n");
        return;
    }

    err = zigbee_manager_leave_network();
    if(err != ESP_OK) {
        zigbee_write_error(w, err);
        return;
    }

    command_context_write_line(w->context, "ZIGBEE_LEFT\n");
}

static void zigbee_handle_trigger(zigbee_writer_t* w, const char* args) {
    char id_raw[ZIGBEE_VALUE_MAX];
    char index_raw[ZIGBEE_VALUE_MAX];
    uint32_t profile_id;
    uint32_t index;
    uint32_t counter;
    esp_err_t err;

    if(!zigbee_read_named_value(args, "id", id_raw, sizeof(id_raw)) ||
       !zigbee_parse_u32(id_raw, &profile_id) ||
       !zigbee_read_named_value(args, "index", index_raw, sizeof(index_raw)) ||
       !zigbee_parse_u32(index_raw, &index) ||
       index == 0U || index > ZIGBEE_BUTTONS_PER_PROFILE) {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE TRIGGER id=<id> index=<1-8>\n");
        return;
    }

    err = zigbee_manager_trigger_button(profile_id, (uint8_t)(index - 1U), &counter);
    if(err != ESP_OK) {
        zigbee_write_error(w, err);
        return;
    }

    snprintf(w->line, sizeof(w->line), "ZIGBEE_TRIGGERED counter=%u\n", (unsigned)counter);
    command_context_write_line(w->context, w->line);
}

static void zigbee_handle_profile(zigbee_writer_t* w, const char* args) {
    char action[ZIGBEE_SUBCOMMAND_MAX];
    const char* rest = args;

    if(!zigbee_read_token(&rest, action, sizeof(action))) {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE PROFILE <CREATE|RENAME|DELETE> ...\n");
        return;
    }

    rest = zigbee_skip_spaces(rest);

    if(strcmp(action, "CREATE") == 0) {
        zigbee_handle_profile_create(w, rest);
        return;
    }

    if(strcmp(action, "RENAME") == 0) {
        zigbee_handle_profile_rename(w, rest);
        return;
    }

    if(strcmp(action, "DELETE") == 0) {
        zigbee_handle_profile_delete(w, rest);
        return;
    }

    command_context_write_line(w->context, "ERR UNKNOWN_ZIGBEE_PROFILE_COMMAND\n");
}

static void zigbee_handle_button(zigbee_writer_t* w, const char* args) {
    char action[ZIGBEE_SUBCOMMAND_MAX];
    const char* rest = args;

    if(!zigbee_read_token(&rest, action, sizeof(action))) {
        command_context_write_line(w->context, "ERR USAGE ZIGBEE BUTTON RENAME ...\n");
        return;
    }

    rest = zigbee_skip_spaces(rest);

    if(strcmp(action, "RENAME") == 0) {
        zigbee_handle_button_rename(w, rest);
        return;
    }

    command_context_write_line(w->context, "ERR UNKNOWN_ZIGBEE_BUTTON_COMMAND\n");
}

bool zigbee_command_try_handle(const char* command_line, const command_context_t* context) {
    const char* rest;
    char subcommand[ZIGBEE_SUBCOMMAND_MAX];
    zigbee_writer_t writer;

    if(command_line == NULL || context == NULL) {
        return false;
    }

    if(!zigbee_command_matches_prefix(command_line)) {
        return false;
    }

    if(!system_mode_guard_command(context, SYSTEM_MODE_ZIGBEE, "ZIGBEE")) {
        return true;
    }

    writer.context = context;
    writer.line[0] = '\0';

    if(zigbee_manager_init() != ESP_OK) {
        command_context_write_line(context, "ERR ZIGBEE_INIT_FAILED\n");
        return true;
    }

    if(command_line[6] == '\0') {
        command_context_write_line(
            context,
            "ERR USAGE ZIGBEE <STATUS|LIST|PROFILE|BUTTON|SCAN|JOIN|LEAVE|TRIGGER>\n");
        return true;
    }

    rest = command_line + 7;
    if(!zigbee_read_token(&rest, subcommand, sizeof(subcommand))) {
        command_context_write_line(
            context,
            "ERR USAGE ZIGBEE <STATUS|LIST|PROFILE|BUTTON|JOIN|LEAVE|TRIGGER>\n");
        return true;
    }

    rest = zigbee_skip_spaces(rest);

    if(strcmp(subcommand, "STATUS") == 0) {
        zigbee_handle_status(&writer, rest);
        return true;
    }

    if(strcmp(subcommand, "LIST") == 0) {
        zigbee_handle_list(&writer, rest);
        return true;
    }

    if(strcmp(subcommand, "PROFILE") == 0) {
        zigbee_handle_profile(&writer, rest);
        return true;
    }

    if(strcmp(subcommand, "BUTTON") == 0) {
        zigbee_handle_button(&writer, rest);
        return true;
    }

    if(strcmp(subcommand, "SCAN") == 0) {
        zigbee_handle_scan(&writer, rest);
        return true;
    }

    if(strcmp(subcommand, "JOIN") == 0) {
        zigbee_handle_join(&writer, rest);
        return true;
    }

    if(strcmp(subcommand, "LEAVE") == 0) {
        zigbee_handle_leave(&writer, rest);
        return true;
    }

    if(strcmp(subcommand, "TRIGGER") == 0) {
        zigbee_handle_trigger(&writer, rest);
        return true;
    }

    command_context_write_line(context, "ERR UNKNOWN_ZIGBEE_COMMAND\n");
    return true;
}
