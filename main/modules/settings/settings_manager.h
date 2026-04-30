#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/* ---------------------------------------------------------------------------
 * Settings value struct — the live in-memory representation.
 * To add a new setting: add a field here AND a matching row in
 * settings_manager.c's s_entries[] table. No other files need changing.
 * --------------------------------------------------------------------------- */
typedef struct {
    bool log_wifi_packets;
    bool log_ble_packets;
    bool ble_watch_recurring;
} settings_values_t;

typedef enum {
    SETTINGS_TYPE_BOOL = 0,
    SETTINGS_TYPE_INT,
} settings_type_t;

/* Public view of a single setting descriptor — used by the CONFIG command. */
typedef struct {
    const char*    key;
    const char*    label;
    settings_type_t type;
    size_t         offset; /* offsetof field within settings_values_t */
} settings_descriptor_t;

typedef void (*settings_iterator_t)(
    const settings_descriptor_t* desc,
    const settings_values_t*     values,
    void*                        context);

/* ---------------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------------- */

/*
 * Initialise the SD SPI bus, mount the card, then load or create
 * /settings/config.ini.  Always returns ESP_OK — if the SD is unavailable,
 * defaults are applied and sd_available is set to false.
 */
esp_err_t settings_manager_init(void);

void settings_manager_deinit(void);

/* True if the SD card was present and mounted during init. */
bool settings_manager_sd_available(void);

/*
 * Returns a snapshot copy of the current in-memory settings.  Thread-safe.
 */
settings_values_t settings_manager_get(void);

/*
 * Update one bool setting in-memory and flush the full config to SD.
 *   ESP_ERR_NOT_FOUND    — key is unknown
 *   ESP_ERR_INVALID_STATE — SD unavailable (in-memory value IS still updated)
 *   ESP_FAIL             — SD write error
 */
esp_err_t settings_manager_set_bool(const char* key, bool value);

/*
 * Reset all settings to defaults and flush to SD.
 *   ESP_ERR_INVALID_STATE — SD unavailable
 */
esp_err_t settings_manager_reset(void);

/*
 * Iterate all setting descriptors paired with the current values snapshot.
 * Used by settings_command to build CONFIG_ENTRY lines.
 */
void settings_manager_iterate(settings_iterator_t callback, void* context);
