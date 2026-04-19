#pragma once

#include <stdbool.h>

#include "command/command_context.h"

bool wifi_beacon_command_try_handle(const char *args, const command_context_t *context);
