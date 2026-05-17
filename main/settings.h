#pragma once

#include <stdbool.h>
#include "types.h"

/*
 * settings.h — Key/value settings stored on the SD card.
 *
 * settings_init() loads values from /sdcard/settings.txt if the card is
 * mounted; otherwise it silently applies built-in defaults so the rest of
 * the firmware can always call settings_get() safely.
 *
 * settings_handle_command() serves the SETTINGS_GET and SETTINGS_SET
 * UART commands.
 */

/* Load settings from SD if mounted, otherwise apply defaults. */
void settings_init(sd_state_t *sd);

/* Route a SETTINGS_* UART command and send the response. */
void settings_handle_command(const parsed_cmd_t *cmd, sd_state_t *sd);

/* Return the value for key, or default_value if the key is not set. */
const char *settings_get(const char *key, const char *default_value);

/* Persist a key/value pair to SD.
 * Returns false if the card is not mounted or the write fails. */
bool settings_set(const char *key, const char *value, sd_state_t *sd);
