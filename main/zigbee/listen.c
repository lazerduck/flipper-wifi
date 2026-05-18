#include "zigbee.h"
#include "uart.h"

void zigbee_listen_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: parse channel="..." from args; receive raw IEEE 802.15.4 frames and stream
    //       each as a DATA line until SYS_STOP sets active_op->stop_requested
    (void)cmd;
    (void)token_offset;
    (void)state;
}
