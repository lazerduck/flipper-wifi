#include "settings.h"
#include "sd_card.h"
#include "uart.h"

void settings_init(sd_state_t *sd)
{
    // TODO: if sd->mounted, read /sdcard/settings.txt into an in-memory key/value table;
    //       any missing keys fall back to built-in defaults
}

void settings_handle_command(const parsed_cmd_t *cmd, sd_state_t *sd)
{
    // TODO: inspect token after "SETTINGS"; dispatch to SETTINGS_GET or SETTINGS_SET
    (void)cmd;
    (void)sd;
}

const char *settings_get(const char *key, const char *default_value)
{
    // TODO: look up key in the in-memory table; return its value or default_value
    (void)key;
    return default_value;
}

bool settings_set(const char *key, const char *value, sd_state_t *sd)
{
    // TODO: update the in-memory table; persist the full table back to /sdcard/settings.txt
    (void)key;
    (void)value;
    (void)sd;
    return false;
}
