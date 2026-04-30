#pragma once

#include "command/command_context.h"

/*
 * Handles the CONFIG command family:
 *   CONFIG GET
 *   CONFIG SET key=<key> value=<0|1>
 *   CONFIG RESET
 *
 * Returns true if the command was consumed, false otherwise.
 */
bool settings_command_try_handle(const char* command_line, const command_context_t* context);
