#include <string.h>
#include "parser.h"

bool parser_parse(const char *line, parsed_cmd_t *out)
{
    // TODO: copy line into out->scratch; strip \r\n; split command name by '_'
    //       into out->tokens; parse key="value" pairs into out->args
    (void)line;
    (void)out;
    return false;
}

const char *parser_get_arg(const parsed_cmd_t *cmd, const char *key)
{
    // TODO: linear scan of cmd->args for a matching key; return value or NULL
    (void)cmd;
    (void)key;
    return NULL;
}

void parser_free(parsed_cmd_t *cmd)
{
    // TODO: zero the struct so it is safe to reuse for the next line
    (void)cmd;
}
