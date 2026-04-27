#include "modules/sd/sd_command.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"

#include "modules/sd/sd_card_manager.h"

#define SD_COMMAND_PREFIX "SD"
#define SD_SUBCOMMAND_MAX 16
#define SD_ARG_VALUE_MAX 192

typedef enum {
    SD_READ_ARG_NOT_FOUND = 0,
    SD_READ_ARG_FOUND,
    SD_READ_ARG_INVALID,
} sd_read_arg_result_t;

typedef struct {
    const command_context_t* command_context;
} sd_writer_context_t;

static const char* sd_skip_spaces(const char* cursor) {
    while(cursor != NULL && *cursor == ' ') {
        ++cursor;
    }

    return cursor;
}

static bool sd_copy_quoted_value(const char** cursor, char* value, size_t value_size) {
    size_t value_length = 0;
    const char* read_cursor;

    if(cursor == NULL || *cursor == NULL || value == NULL || value_size == 0 || **cursor != '"') {
        return false;
    }

    read_cursor = *cursor + 1;
    while(*read_cursor != '\0') {
        char current = *read_cursor;

        if(current == '"') {
            value[value_length] = '\0';
            ++read_cursor;
            if(*read_cursor != '\0' && *read_cursor != ' ') {
                return false;
            }

            *cursor = read_cursor;
            return true;
        }

        if(current == '\\') {
            ++read_cursor;
            if(*read_cursor == '\0') {
                return false;
            }

            current = *read_cursor;
        }

        if(value_length + 1U >= value_size) {
            return false;
        }

        value[value_length++] = current;
        ++read_cursor;
    }

    return false;
}

static bool sd_copy_unquoted_value(const char** cursor, char* value, size_t value_size) {
    size_t value_length = 0;
    const char* read_cursor;

    if(cursor == NULL || *cursor == NULL || value == NULL || value_size == 0) {
        return false;
    }

    read_cursor = *cursor;
    while(*read_cursor != '\0' && *read_cursor != ' ') {
        if(value_length + 1U >= value_size) {
            return false;
        }

        value[value_length++] = *read_cursor;
        ++read_cursor;
    }

    if(value_length == 0U) {
        return false;
    }

    value[value_length] = '\0';
    *cursor = read_cursor;
    return true;
}

static sd_read_arg_result_t sd_read_named_arg(
    const char* args,
    const char* key,
    char* value,
    size_t value_size) {
    const char* cursor = args;
    size_t key_length;

    if(args == NULL || key == NULL || value == NULL || value_size == 0U) {
        return SD_READ_ARG_INVALID;
    }

    key_length = strlen(key);
    value[0] = '\0';

    while(*cursor != '\0') {
        const char* key_end;

        cursor = sd_skip_spaces(cursor);
        if(*cursor == '\0') {
            break;
        }

        key_end = strchr(cursor, '=');
        if(key_end == NULL) {
            return SD_READ_ARG_INVALID;
        }

        if((size_t)(key_end - cursor) == key_length && strncmp(cursor, key, key_length) == 0) {
            const char* value_cursor = key_end + 1;
            bool ok;

            if(*value_cursor == '"') {
                ok = sd_copy_quoted_value(&value_cursor, value, value_size);
            } else {
                ok = sd_copy_unquoted_value(&value_cursor, value, value_size);
            }

            return ok ? SD_READ_ARG_FOUND : SD_READ_ARG_INVALID;
        }

        cursor = key_end + 1;
        if(*cursor == '"') {
            if(!sd_copy_quoted_value(&cursor, value, value_size)) {
                return SD_READ_ARG_INVALID;
            }
        } else if(!sd_copy_unquoted_value(&cursor, value, value_size)) {
            return SD_READ_ARG_INVALID;
        }

        cursor = sd_skip_spaces(cursor);
    }

    return SD_READ_ARG_NOT_FOUND;
}

static bool sd_parse_u32(const char* value, uint32_t* result) {
    char* end = NULL;
    unsigned long parsed;

    if(value == NULL || result == NULL || value[0] == '\0') {
        return false;
    }

    parsed = strtoul(value, &end, 10);
    if(end == NULL || *end != '\0') {
        return false;
    }

    *result = (uint32_t)parsed;
    return true;
}

static void sd_write_line(const char* line, void* context) {
    const sd_writer_context_t* writer_context = (const sd_writer_context_t*)context;

    if(writer_context == NULL || writer_context->command_context == NULL) {
        return;
    }

    command_context_write_line(writer_context->command_context, line);
}

static void sd_write_mount_error(const command_context_t* context, esp_err_t err) {
    char line[96];

    if(context == NULL) {
        return;
    }

    if(err == ESP_ERR_NOT_FOUND) {
        command_context_write_line(context, "ERR SD_CARD_NOT_PRESENT\n");
        return;
    }

    if(err == ESP_ERR_INVALID_STATE) {
        command_context_write_line(context, "ERR SD_NOT_READY\n");
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "ERR SD_IO code=0x%X name=%s\n",
        (unsigned)err,
        esp_err_to_name(err));
    command_context_write_line(context, line);
}

bool sd_command_try_handle(const char* command_line, const command_context_t* context) {
    const char* subcommand;
    const char* args = "";
    char subcommand_name[SD_SUBCOMMAND_MAX];
    size_t subcommand_length;

    if(command_line == NULL || context == NULL) {
        return false;
    }

    if(strncmp(command_line, SD_COMMAND_PREFIX, strlen(SD_COMMAND_PREFIX)) != 0) {
        return false;
    }

    if(command_line[2] != '\0' && command_line[2] != ' ') {
        return false;
    }

    if(command_line[2] == '\0') {
        command_context_write_line(
            context,
            "ERR USAGE SD <INFO|INIT|LIST|FORMAT|MKDIR|APPEND|LOG>\n");
        return true;
    }

    subcommand = command_line + 3;
    args = strchr(subcommand, ' ');
    if(args == NULL) {
        subcommand_length = strlen(subcommand);
        args = "";
    } else {
        subcommand_length = (size_t)(args - subcommand);
        ++args;
    }

    if(subcommand_length == 0U || subcommand_length >= sizeof(subcommand_name)) {
        command_context_write_line(context, "ERR UNKNOWN_SD_COMMAND\n");
        return true;
    }

    memcpy(subcommand_name, subcommand, subcommand_length);
    subcommand_name[subcommand_length] = '\0';

    if(strcmp(subcommand_name, "INFO") == 0) {
        sd_card_manager_info_t info;
        char line[320];
        esp_err_t err;

        if(args[0] != '\0') {
            command_context_write_line(context, "ERR USAGE SD INFO\n");
            return true;
        }

        err = sd_card_manager_get_info(&info);
        if(err != ESP_OK) {
            sd_write_mount_error(context, err);
            return true;
        }

        snprintf(
            line,
            sizeof(line),
            "SD INFO present=%s mounted=%s total_bytes=%llu free_bytes=%llu\n",
            info.card_present ? "yes" : "no",
            info.mounted ? "yes" : "no",
            (unsigned long long)info.total_bytes,
            (unsigned long long)info.free_bytes);
        command_context_write_line(context, line);
        return true;
    }

    if(strcmp(subcommand_name, "INIT") == 0) {
        esp_err_t err;

        if(args[0] != '\0') {
            command_context_write_line(context, "ERR USAGE SD INIT\n");
            return true;
        }

        command_context_write_line(context, "SD INIT_START\n");
        command_context_write_line(context, "SD INIT_PROGRESS pct=20 step=mount\n");

        err = sd_card_manager_initialize();
        if(err != ESP_OK) {
            sd_write_mount_error(context, err);
            return true;
        }

        command_context_write_line(context, "SD INIT_PROGRESS pct=100 step=done\n");
        command_context_write_line(context, "SD INIT_DONE status=ok\n");
        return true;
    }

    if(strcmp(subcommand_name, "LIST") == 0) {
        char path[SD_ARG_VALUE_MAX];
        char limit_text[16];
        uint32_t limit = 48U;
        sd_card_manager_list_result_t result;
        sd_writer_context_t writer_context = {
            .command_context = context,
        };
        char line[320];
        esp_err_t err;

        if(sd_read_named_arg(args, "path", path, sizeof(path)) == SD_READ_ARG_INVALID) {
            command_context_write_line(context, "ERR USAGE SD LIST [path=/] [limit=48]\n");
            return true;
        }

        if(sd_read_named_arg(args, "limit", limit_text, sizeof(limit_text)) == SD_READ_ARG_FOUND) {
            if(!sd_parse_u32(limit_text, &limit) || limit == 0U || limit > 256U) {
                command_context_write_line(context, "ERR USAGE SD LIST [path=/] [limit=48]\n");
                return true;
            }
        }

        if(path[0] == '\0') {
            snprintf(path, sizeof(path), "/");
        }

        snprintf(line, sizeof(line), "SD LIST_BEGIN path=%s\n", path);
        command_context_write_line(context, line);

        err = sd_card_manager_list_directory(path, (size_t)limit, sd_write_line, &writer_context, &result);
        if(err != ESP_OK) {
            sd_write_mount_error(context, err);
            return true;
        }

        snprintf(
            line,
            sizeof(line),
            "SD LIST_DONE count=%lu total=%lu truncated=%lu\n",
            (unsigned long)result.entries_emitted,
            (unsigned long)result.entries_total,
            (unsigned long)result.entries_truncated);
        command_context_write_line(context, line);
        return true;
    }

    if(strcmp(subcommand_name, "FORMAT") == 0) {
        char full_text[16];
        bool full_format = false;
        esp_err_t err;

        if(args[0] != '\0') {
            sd_read_arg_result_t full_arg =
                sd_read_named_arg(args, "full", full_text, sizeof(full_text));

            if(full_arg == SD_READ_ARG_INVALID) {
                command_context_write_line(context, "ERR USAGE SD FORMAT [full=1]\n");
                return true;
            }

            if(full_arg == SD_READ_ARG_FOUND) {
                uint32_t parsed = 0;
                if(!sd_parse_u32(full_text, &parsed) || parsed > 1U) {
                    command_context_write_line(context, "ERR USAGE SD FORMAT [full=1]\n");
                    return true;
                }

                full_format = (parsed == 1U);
            }
        }

        command_context_write_line(context, "SD FORMAT_START\n");
        if(full_format) {
            command_context_write_line(context, "SD FORMAT_PROGRESS pct=10 step=mount\n");
        } else {
            command_context_write_line(context, "SD FORMAT_PROGRESS pct=10 step=init\n");
        }

        // Default behavior is quick initialize for new cards, which avoids long
        // blocking full-media format operations. Full format is opt-in.
        if(full_format) {
            err = sd_card_manager_format();
        } else {
            err = sd_card_manager_initialize();
        }

        if(err != ESP_OK) {
            sd_write_mount_error(context, err);
            return true;
        }

        if(full_format) {
            command_context_write_line(context, "SD FORMAT_PROGRESS pct=85 step=folders\n");
            command_context_write_line(context, "SD FORMAT_DONE status=ok mode=full\n");
        } else {
            command_context_write_line(context, "SD FORMAT_PROGRESS pct=90 step=folders\n");
            command_context_write_line(context, "SD FORMAT_DONE status=ok mode=quick\n");
        }
        return true;
    }

    if(strcmp(subcommand_name, "MKDIR") == 0) {
        char path[SD_ARG_VALUE_MAX];
        char line[320];
        esp_err_t err;

        if(sd_read_named_arg(args, "path", path, sizeof(path)) != SD_READ_ARG_FOUND) {
            command_context_write_line(context, "ERR USAGE SD MKDIR path=/logs/custom\n");
            return true;
        }

        err = sd_card_manager_ensure_directory(path);
        if(err != ESP_OK) {
            sd_write_mount_error(context, err);
            return true;
        }

        snprintf(line, sizeof(line), "SD MKDIR_OK path=%s\n", path);
        command_context_write_line(context, line);
        return true;
    }

    if(strcmp(subcommand_name, "APPEND") == 0) {
        char path[SD_ARG_VALUE_MAX];
        char text[SD_ARG_VALUE_MAX];
        char line[320];
        esp_err_t err;

        if(sd_read_named_arg(args, "path", path, sizeof(path)) != SD_READ_ARG_FOUND ||
           sd_read_named_arg(args, "line", text, sizeof(text)) != SD_READ_ARG_FOUND) {
            command_context_write_line(context, "ERR USAGE SD APPEND path=/logs/raw.txt line=\"text\"\n");
            return true;
        }

        err = sd_card_manager_append_text_file(path, text);
        if(err != ESP_OK) {
            sd_write_mount_error(context, err);
            return true;
        }

        snprintf(line, sizeof(line), "SD APPEND_OK path=%s\n", path);
        command_context_write_line(context, line);
        return true;
    }

    if(strcmp(subcommand_name, "LOG") == 0) {
        char module[48];
        char text[SD_ARG_VALUE_MAX];
        char relative_path[96];
        char line[320];
        esp_err_t err;

        if(sd_read_named_arg(args, "module", module, sizeof(module)) != SD_READ_ARG_FOUND ||
           sd_read_named_arg(args, "line", text, sizeof(text)) != SD_READ_ARG_FOUND) {
            command_context_write_line(context, "ERR USAGE SD LOG module=<name> line=\"text\"\n");
            return true;
        }

        err = sd_card_manager_append_module_log(module, text, relative_path, sizeof(relative_path));
        if(err != ESP_OK) {
            sd_write_mount_error(context, err);
            return true;
        }

        snprintf(line, sizeof(line), "SD LOG_OK module=%s path=%s\n", module, relative_path);
        command_context_write_line(context, line);
        return true;
    }

    command_context_write_line(context, "ERR UNKNOWN_SD_COMMAND\n");
    return true;
}
