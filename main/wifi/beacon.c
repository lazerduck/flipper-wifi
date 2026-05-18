#include "wifi.h"
#include "uart.h"

void wifi_beacon_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: parse ssids="..." array from cmd args; begin transmitting forged beacon frames
    //       on the current channel until SYS_STOP sets active_op->stop_requested
    (void)cmd;
    (void)token_offset;
    (void)state;
}
