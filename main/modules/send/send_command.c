#include "modules/send/send_command.h"

#include <string.h>

bool send_command_try_handle(const char *command_line, const command_context_t *context)
{
    if (command_line == NULL || context == NULL) {
        return false;
    }

    if (strcmp(command_line, "SEND") == 0) {
        command_context_write_line(context, "ERR USAGE SEND <payload>\n");
        return true;
    }

    if (strncmp(command_line, "SEND ", strlen("SEND ")) == 0) {
        command_context_write_line(context, "ERR SEND_NOT_IMPLEMENTED\n");
        return true;
    }

    return false;
}