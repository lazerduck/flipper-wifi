#include "zigbee.h"
#include "uart.h"

void zigbee_scan_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: parse channels="..." and duration="..." from args; sample packet activity on each
    //       channel; stream a DATA line per channel (channel, packets); send END when done
    (void)cmd;
    (void)token_offset;
    (void)state;
}
