#include <string.h>
#include "parser.h"

bool parser_parse(const char *line, parsed_cmd_t *out)
{
    if (!line || !out) return false;

    memset(out, 0, sizeof(*out));

    size_t len = strlen(line);
    if (len == 0 || len >= sizeof(out->scratch)) return false;
    memcpy(out->scratch, line, len + 1);

    /* Strip trailing \r and \n */
    while (len > 0 && (out->scratch[len - 1] == '\r' || out->scratch[len - 1] == '\n')) {
        out->scratch[--len] = '\0';
    }
    if (len == 0) return false;

    /* Split command name from arguments on the first space */
    char *args_start = NULL;
    char *space = strchr(out->scratch, ' ');
    if (space) {
        *space    = '\0';
        args_start = space + 1;
    }

    /* Reject empty command names (e.g. line was only whitespace) */
    if (out->scratch[0] == '\0') return false;

    /* Tokenise command name on '_' */
    out->tokens[out->token_count++] = out->scratch;
    for (char *c = out->scratch; *c; c++) {
        if (*c == '_') {
            *c = '\0';
            if (out->token_count < PARSED_CMD_MAX_TOKENS) {
                out->tokens[out->token_count++] = c + 1;
            }
        }
    }

    /* Parse key="value" pairs */
    char *p = args_start;
    while (p && *p && out->arg_count < PARSED_CMD_MAX_ARGS) {
        while (*p == ' ') p++;
        if (*p == '\0') break;

        /* Key up to '=' */
        char *key = p;
        while (*p && *p != '=') p++;
        if (*p != '=') break;
        *p++ = '\0';

        /* Opening quote */
        if (*p != '"') break;
        p++;

        /* Value — scan for closing unescaped '"', unescape \" in-place */
        char *value = p;
        char *dst   = p;
        while (*p && *p != '"') {
            if (*p == '\\' && *(p + 1) == '"') {
                *dst++ = '"';
                p += 2;
            } else {
                *dst++ = *p++;
            }
        }
        if (*p != '"') break; /* unterminated value */
        *dst = '\0';
        p++; /* skip closing quote */

        out->args[out->arg_count].key   = key;
        out->args[out->arg_count].value = value;
        out->arg_count++;
    }

    return true;
}

const char *parser_get_arg(const parsed_cmd_t *cmd, const char *key)
{
    for (int i = 0; i < cmd->arg_count; i++) {
        if (strcmp(cmd->args[i].key, key) == 0) {
            return cmd->args[i].value;
        }
    }
    return NULL;
}

void parser_free(parsed_cmd_t *cmd)
{
    memset(cmd, 0, sizeof(*cmd));
}
