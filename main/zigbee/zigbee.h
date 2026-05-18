#pragma once

#include "types.h"

/*
 * zigbee.h — ZigBee module: initialisation, deinitialisation, and command routing.
 *
 * zigbee_init() starts the IEEE 802.15.4 / ZigBee stack.
 * zigbee_deinit() tears it down cleanly before a SYS_RESTART.
 *
 * zigbee_handle_command() is called by the router when the first token is
 * "ZIGBEE".  It consumes the next token to identify the sub-command
 * (CHANNEL, LISTEN) and delegates to scan.c or listen.c.
 *
 * token_offset is the index of the token this layer should inspect.
 * The router passes 1 (having consumed token[0] = "ZIGBEE").
 *
 * Note: RF stack integration is not yet implemented; init/deinit are stubs.
 */

void zigbee_init(app_state_t *state);
void zigbee_deinit(app_state_t *state);
void zigbee_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);
