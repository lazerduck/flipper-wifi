#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYSTEM_MODE_WIFI = 0,
    SYSTEM_MODE_ZIGBEE = 1,
} system_mode_t;

esp_err_t system_mode_manager_init(void);
esp_err_t system_mode_manager_get_active_mode(system_mode_t* out_mode);
esp_err_t system_mode_manager_set_active_mode(system_mode_t mode);

bool system_mode_manager_is_valid(system_mode_t mode);
const char* system_mode_manager_mode_to_string(system_mode_t mode);

#ifdef __cplusplus
}
#endif
