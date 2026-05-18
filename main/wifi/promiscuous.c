#include "wifi.h"
#include "uart.h"

void wifi_promiscuous_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: inspect cmd->tokens[token_offset]; dispatch SCAN (channel activity counts)
    //       or DEVICE (unique device detection) sub-commands; stream results via uart_send_data()
    (void)cmd;
    (void)token_offset;
    (void)state;
}
