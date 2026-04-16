#include "modules/query/query_command.h"

#include <string.h>

bool query_command_try_handle(const char *command_line, const command_context_t *context)
{
    if (command_line == NULL || context == NULL) {
        return false;
    }

    if (strcmp(command_line, "QUERY") == 0) {
        command_context_write_line(context, "ERR USAGE QUERY <request>\n");
        return true;
    }

    if (strncmp(command_line, "QUERY ", strlen("QUERY ")) == 0) {
        command_context_write_line(context, "ERR QUERY_NOT_IMPLEMENTED\n");
        return true;
    }

    return false;
}