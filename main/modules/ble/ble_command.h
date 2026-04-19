#pragma once

#include <stdbool.h>

#include "command/command_context.h"

typedef void (*ble_scan_result_writer_t)(const char *line, void *context);

bool ble_command_try_handle(const char *command_line, const command_context_t *context);