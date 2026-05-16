#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ZIGBEE_PROFILE_NAME_MAX 31U
#define ZIGBEE_BUTTON_NAME_MAX 23U
#define ZIGBEE_BUTTONS_PER_PROFILE 8U
#define ZIGBEE_MAX_PROFILES 16U

typedef struct {
    char name[ZIGBEE_BUTTON_NAME_MAX + 1U];
} zigbee_button_t;

typedef struct {
    uint32_t id;
    uint16_t pan_id;
    uint8_t channel;
    bool joined;
    char name[ZIGBEE_PROFILE_NAME_MAX + 1U];
    zigbee_button_t buttons[ZIGBEE_BUTTONS_PER_PROFILE];
} zigbee_profile_t;

typedef struct {
    bool initialized;
    bool joined;
    bool sd_available;
    uint32_t active_profile_id;
    uint32_t tx_frame_counter;
    uint16_t active_pan_id;
    uint8_t active_channel;
    size_t profile_count;
} zigbee_manager_status_t;

esp_err_t zigbee_manager_init(void);

esp_err_t zigbee_manager_create_profile(const char* name, uint32_t* out_profile_id);
esp_err_t zigbee_manager_rename_profile(uint32_t profile_id, const char* name);
esp_err_t zigbee_manager_rename_button(uint32_t profile_id, uint8_t button_index, const char* name);
esp_err_t zigbee_manager_delete_profile(uint32_t profile_id);

esp_err_t zigbee_manager_join_profile(uint32_t profile_id, uint16_t pan_id, uint8_t channel);
esp_err_t zigbee_manager_leave_network(void);
esp_err_t zigbee_manager_trigger_button(uint32_t profile_id, uint8_t button_index, uint32_t* out_counter);

esp_err_t zigbee_manager_get_status(zigbee_manager_status_t* out_status);
esp_err_t zigbee_manager_list_profiles(const zigbee_profile_t** out_profiles, size_t* out_count);
