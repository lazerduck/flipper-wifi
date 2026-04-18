#pragma once

#include <stdbool.h>

#include "command/command_context.h"

bool wifi_connected_command_try_handle(
    const char *subcommand_name,
    const char *args,
    const command_context_t *context);
