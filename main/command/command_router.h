#pragma once

#include "command/command_context.h"

void command_router_dispatch(const char *command_line, const command_context_t *context);