#include "modules/system/system_mode_guard.h"

#include <stdio.h>

bool system_mode_guard_command(
    const command_context_t* context,
    system_mode_t expected_mode,
    const char* command_family) {
    system_mode_t active_mode = SYSTEM_MODE_WIFI;
    esp_err_t mode_err;
    char line[112];

    if(context == NULL || command_family == NULL) {
        return false;
    }

    mode_err = system_mode_manager_get_active_mode(&active_mode);
    if(mode_err != ESP_OK) {
        snprintf(
            line,
            sizeof(line),
            "ERR SYSTEM_MODE_UNAVAILABLE command=%s code=0x%X\n",
            command_family,
            (unsigned)mode_err);
        command_context_write_line(context, line);
        return false;
    }

    if(active_mode != expected_mode) {
        snprintf(
            line,
            sizeof(line),
            "ERR MODE_MISMATCH active=%s expected=%s command=%s\n",
            system_mode_manager_mode_to_string(active_mode),
            system_mode_manager_mode_to_string(expected_mode),
            command_family);
        command_context_write_line(context, line);
        return false;
    }

    return true;
}
