#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "modules/zigbee/zigbee_manager.h"

esp_err_t zigbee_profile_store_load(
    zigbee_profile_t* profiles,
    size_t max_profiles,
    size_t* out_count,
    uint32_t* out_next_id,
    bool* out_sd_available);

esp_err_t zigbee_profile_store_save(
    const zigbee_profile_t* profiles,
    size_t profile_count,
    uint32_t next_id);
