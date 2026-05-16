#include "modules/system/system_command.h"

#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "modules/system/system_mode_manager.h"

#define SYSTEM_SET_REBOOT_DELAY_MS 150U

static bool system_command_matches_prefix(const char* command_line) {
    if(strncmp(command_line, "SYSTEM", 6) != 0) {
        return false;
    }

    return command_line[6] == '\0' || command_line[6] == ' ';
}

static void system_write_mode_line(const command_context_t* context, const char* prefix, system_mode_t mode) {
    char line[80];

    snprintf(line, sizeof(line), "%s mode=%s\n", prefix, system_mode_manager_mode_to_string(mode));
    command_context_write_line(context, line);
}

static void system_handle_mode_query(const command_context_t* context, const char* args) {
    system_mode_t mode;

    if(args[0] != '\0') {
        command_context_write_line(context, "ERR USAGE SYSTEM MODE QUERY\n");
        return;
    }

    if(system_mode_manager_get_active_mode(&mode) != ESP_OK) {
        command_context_write_line(context, "ERR SYSTEM_MODE_QUERY_FAILED\n");
        return;
    }

    system_write_mode_line(context, "SYSTEM_MODE", mode);
}

static bool system_parse_mode_arg(const char* raw_mode, system_mode_t* out_mode) {
    if(raw_mode == NULL || out_mode == NULL) {
        return false;
    }

    if(strcmp(raw_mode, "WIFI") == 0) {
        *out_mode = SYSTEM_MODE_WIFI;
        return true;
    }

    if(strcmp(raw_mode, "ZIGBEE") == 0) {
        *out_mode = SYSTEM_MODE_ZIGBEE;
        return true;
    }

    return false;
}

static void system_handle_mode_set(const command_context_t* context, const char* args) {
    system_mode_t current_mode;
    system_mode_t requested_mode;
    char mode_token[16] = {0};
    char extra_token[8] = {0};

    if(args == NULL || args[0] == '\0') {
        command_context_write_line(context, "ERR USAGE SYSTEM MODE SET <WIFI|ZIGBEE>\n");
        return;
    }

    if(sscanf(args, "%15s %7s", mode_token, extra_token) != 1) {
        command_context_write_line(context, "ERR USAGE SYSTEM MODE SET <WIFI|ZIGBEE>\n");
        return;
    }

    if(!system_parse_mode_arg(mode_token, &requested_mode)) {
        command_context_write_line(context, "ERR INVALID_MODE\n");
        return;
    }

    if(system_mode_manager_get_active_mode(&current_mode) != ESP_OK) {
        command_context_write_line(context, "ERR SYSTEM_MODE_QUERY_FAILED\n");
        return;
    }

    if(current_mode == requested_mode) {
        system_write_mode_line(context, "SYSTEM_MODE_UNCHANGED", current_mode);
        return;
    }

    if(system_mode_manager_set_active_mode(requested_mode) != ESP_OK) {
        command_context_write_line(context, "ERR SYSTEM_MODE_SET_FAILED\n");
        return;
    }

    {
        char line[96];
        snprintf(
            line,
            sizeof(line),
            "SYSTEM_MODE_SET mode=%s rebooting=yes\n",
            system_mode_manager_mode_to_string(requested_mode));
        command_context_write_line(context, line);
    }

    vTaskDelay(pdMS_TO_TICKS(SYSTEM_SET_REBOOT_DELAY_MS));
    esp_restart();
}

bool system_command_try_handle(const char* command_line, const command_context_t* context) {
    const char* rest;

    if(command_line == NULL || context == NULL) {
        return false;
    }

    if(!system_command_matches_prefix(command_line)) {
        return false;
    }

    if(command_line[6] == '\0') {
        command_context_write_line(context, "ERR USAGE SYSTEM MODE <QUERY|SET>\n");
        return true;
    }

    rest = command_line + 7;

    if(strncmp(rest, "MODE QUERY", 10) == 0 && (rest[10] == '\0' || rest[10] == ' ')) {
        const char* args = rest + 10;
        while(*args == ' ') {
            args++;
        }
        system_handle_mode_query(context, args);
        return true;
    }

    if(strncmp(rest, "MODE SET", 8) == 0 && (rest[8] == ' ' || rest[8] == '\0')) {
        const char* args = rest + 8;
        while(*args == ' ') {
            args++;
        }
        system_handle_mode_set(context, args);
        return true;
    }

    command_context_write_line(context, "ERR UNKNOWN_SYSTEM_COMMAND\n");
    return true;
}
