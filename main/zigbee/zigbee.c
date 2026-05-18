#include "zigbee.h"
#include "uart.h"

/* Sub-module handlers — defined in scan.c, listen.c */
void zigbee_scan_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);
void zigbee_listen_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);

void zigbee_init(app_state_t *state)
{
    // TODO: initialise IEEE 802.15.4 / ZigBee stack; set state->mode = APP_MODE_ZIGBEE
}

void zigbee_deinit(app_state_t *state)
{
    // TODO: stop ZigBee stack; free channel_stats if allocated
}

void zigbee_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: inspect cmd->tokens[token_offset]; route CHANNEL_SCAN to zigbee_scan_handle_command,
    //       LISTEN to zigbee_listen_handle_command; unknown token → ERR_UNKNOWN_CMD
    (void)cmd;
    (void)token_offset;
    (void)state;
}
