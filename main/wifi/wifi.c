#include "wifi.h"
#include "uart.h"

/* Sub-module handlers — defined in promiscuous.c, connected.c, beacon.c */
void wifi_promiscuous_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);
void wifi_connected_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);
void wifi_beacon_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state);

void wifi_init(app_state_t *state)
{
    // TODO: call esp_wifi_init(); set state->mode = APP_MODE_WIFI; sub_mode = WIFI_SUB_NONE
}

void wifi_deinit(app_state_t *state)
{
    // TODO: call esp_wifi_stop() and esp_wifi_deinit(); free scan_results if allocated
}

void wifi_handle_command(const parsed_cmd_t *cmd, int token_offset, app_state_t *state)
{
    // TODO: inspect cmd->tokens[token_offset]; route SCAN_SSIDS/CONNECT/IP_SWEEP to
    //       wifi_connected_handle_command; PROMISCUOUS/DEVICE to wifi_promiscuous_handle_command;
    //       BEACON to wifi_beacon_handle_command; unknown token → ERR_UNKNOWN_CMD
    (void)cmd;
    (void)token_offset;
    (void)state;
}
