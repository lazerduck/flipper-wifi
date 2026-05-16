#pragma once

#include <stdbool.h>

#include "command/command_context.h"
#include "modules/system/system_mode_manager.h"

bool system_mode_guard_command(
    const command_context_t* context,
    system_mode_t expected_mode,
    const char* command_family);
