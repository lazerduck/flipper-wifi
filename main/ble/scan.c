#include "ble.h"
#include "uart.h"

void ble_scan_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: parse duration="..." from args; run a passive NimBLE scan; stream each
    //       discovered advertiser as a DATA line (mac, name, rssi); send END when done
    (void)cmd;
    (void)token_offset;
    (void)state;
}
