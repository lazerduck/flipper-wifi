#include "modules/ping/ping_command.h"

#include <string.h>

bool ping_command_try_handle(const char *command_line, const command_context_t *context)
{
    if (command_line == NULL || context == NULL) {
        return false;
    }

    if (strcmp(command_line, "PING") != 0) {
        return false;
    }

    command_context_write_line(context, "PONG\n");
    return true;
}