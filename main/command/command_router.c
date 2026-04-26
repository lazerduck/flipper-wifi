#include "command/command_router.h"

#include "modules/ble/ble_command.h"
#include "modules/ping/ping_command.h"
#include "modules/query/query_command.h"
#include "modules/send/send_command.h"
#include "modules/status_led/status_led_command.h"
#include "modules/wifi/wifi_command.h"

void command_router_dispatch(const char *command_line, const command_context_t *context)
{
    if (command_line == NULL || context == NULL) {
        return;
    }

    if (ping_command_try_handle(command_line, context)) {
        return;
    }

    if (status_led_command_try_handle(command_line, context)) {
        return;
    }

    if (ble_command_try_handle(command_line, context)) {
        return;
    }

    if (wifi_command_try_handle(command_line, context)) {
        return;
    }

    if (send_command_try_handle(command_line, context)) {
        return;
    }

    if (query_command_try_handle(command_line, context)) {
        return;
    }

    command_context_write_line(context, "ERR UNKNOWN_COMMAND\n");
}