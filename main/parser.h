#pragma once

#include <stdbool.h>
#include "types.h"

/*
 * parser.h — Tokenise a raw UART line into a command name and arguments.
 *
 * Input:  "WIFI_PROMISCUOUS_SCAN channels=\"1|6|11\" duration=\"30\"\n"
 * Output: tokens = ["WIFI", "PROMISCUOUS", "SCAN"]
 *         args   = [{key:"channels", value:"1|6|11"}, {key:"duration", value:"30"}]
 *
 * All pointers inside the parsed_cmd_t point into its own scratch buffer,
 * so no separate free is needed for the string data.
 */

/* Parse a single null-terminated line into *out.
 * Returns true on success, false if the line is empty or malformed.
 * The trailing \n or \r\n is stripped automatically. */
bool parser_parse(const char *line, parsed_cmd_t *out);

/* Look up a named argument value; returns NULL if the key is not present.
 * Example: parser_get_arg(&cmd, "channels") → "1|6|11" */
const char *parser_get_arg(const parsed_cmd_t *cmd, const char *key);

/* Reset the struct so it can be reused for the next line. */
void parser_free(parsed_cmd_t *cmd);
