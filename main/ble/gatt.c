#include "ble.h"
#include "uart.h"

void ble_gatt_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: parse mac="..." from args; connect to the device; enumerate GATT services
    //       and characteristics; stream each as a DATA line (uuid, value); disconnect when done
    (void)cmd;
    (void)token_offset;
    (void)state;
}
