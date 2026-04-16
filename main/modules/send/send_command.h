#pragma once

#include <stdbool.h>

#include "command/command_context.h"

bool send_command_try_handle(const char *command_line, const command_context_t *context);