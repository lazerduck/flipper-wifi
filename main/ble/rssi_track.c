#include "ble.h"
#include "uart.h"

void ble_rssi_track_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: parse mac="..." from args; listen for advertisements from that device;
    //       emit a DATA line with rssi on each received advertisement until SYS_STOP
    (void)cmd;
    (void)token_offset;
    (void)state;
}
