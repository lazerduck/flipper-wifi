#include <string.h>
#include "router.h"
#include "uart.h"
#include "sd_card.h"
#include "settings.h"
#include "led.h"
#include "wifi/wifi.h"
#include "ble/ble.h"
#include "zigbee/zigbee.h"

void router_dispatch(const parsed_cmd_t *cmd, app_state_t *state)
{
    // TODO: check cmd->tokens[0]; handle PING, SYS_*, LED_* inline;
    //       forward WIFI_*, BLE_*, ZIGBEE_*, SD_*, SETTINGS_* to their modules;
    //       reply uart_send_error("ERR_UNKNOWN_CMD") for anything unrecognised
    (void)cmd;
    (void)state;
}
