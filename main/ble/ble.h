#pragma once

#include "types.h"

/*
 * ble.h — BLE module: initialisation, deinitialisation, and command routing.
 *
 * ble_init() starts the NimBLE stack and prepares the scan/GATT subsystems.
 * ble_deinit() tears the stack down cleanly before a SYS_RESTART.
 *
 * ble_handle_command() is called by the router when the first token is
 * "BLE".  It consumes the next token to identify the sub-command (SCAN,
 * GATT, RSSI, DEVICE) and delegates to the matching sub-module in
 * scan.c, gatt.c, or rssi_track.c.
 *
 * token_offset is the index of the token this layer should inspect.
 * The router passes 1 (having consumed token[0] = "BLE").
 */

void ble_init(app_state_t *state);
void ble_deinit(app_state_t *state);
void ble_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);
