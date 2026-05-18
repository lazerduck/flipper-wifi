#include "wifi.h"
#include "uart.h"

void wifi_connected_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: inspect cmd->tokens[token_offset]; dispatch SCAN_SSIDS (list networks),
    //       CONNECT (associate and get IP), IP_SWEEP (ping sweep + port check) sub-commands
    (void)cmd;
    (void)token_offset;
    (void)state;
}
