#pragma once

#include "types.h"

/*
 * wifi.h — Wi-Fi module: initialisation, deinitialisation, and command routing.
 *
 * wifi_init() starts the Wi-Fi driver and sets sub_mode to WIFI_SUB_NONE.
 * wifi_deinit() tears the driver down cleanly before a SYS_RESTART.
 *
 * wifi_handle_command() is called by the router when the first token is
 * "WIFI".  It consumes the next token to identify the sub-command (SCAN,
 * CONNECT, PROMISCUOUS, DEVICE, IP, BEACON) and delegates to the matching
 * sub-module in promiscuous.c, connected.c, or beacon.c.
 *
 * token_offset is the index of the token this layer should inspect.
 * The router passes 1 (having consumed token[0] = "WIFI").
 */

void wifi_init(app_state_t *state);
void wifi_deinit(app_state_t *state);
void wifi_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);
