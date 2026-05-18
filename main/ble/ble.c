#include "ble.h"
#include "uart.h"

/* Sub-module handlers — defined in scan.c, gatt.c, rssi_track.c */
void ble_scan_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);
void ble_gatt_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);
void ble_rssi_track_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);

void ble_init(app_state_t *state)
{
    // TODO: initialise NimBLE stack; set state->mode = APP_MODE_BLE
}

void ble_deinit(app_state_t *state)
{
    // TODO: stop NimBLE stack; free scan_results if allocated
}

void ble_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: inspect cmd->tokens[token_offset]; route SCAN to ble_scan_handle_command,
    //       GATT_INSPECT to ble_gatt_handle_command, RSSI_TRACK to ble_rssi_track_handle_command,
    //       DEVICE_SAVE inline; unknown token → ERR_UNKNOWN_CMD
    (void)cmd;
    (void)token_offset;
    (void)state;
}
