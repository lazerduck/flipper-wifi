#pragma once

#include "types.h"

/*
 * router.h — Dispatch a parsed command to the correct module.
 *
 * The router consumes the first token to identify the module, then passes
 * the remaining tokens and args to that module's handle_command function.
 * SYS_*, LED_*, PING, and SD_* commands are handled directly here.
 * WIFI_*, BLE_*, ZIGBEE_*, and SETTINGS_* are forwarded to their modules.
 *
 * Unknown commands reply with E code="ERR_UNKNOWN_CMD".
 */

/* Inspect cmd->tokens[0], route to the appropriate handler, and send the
 * UART response.  state must not be NULL. */
void router_dispatch(const parsed_cmd_t *cmd, app_state_t *state);
