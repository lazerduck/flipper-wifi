#include "modules/zigbee/zigbee_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "modules/zigbee/zigbee_profile_store.h"
#include "modules/zigbee/zigbee_stack.h"

#define ZIGBEE_TAG "zigbee_manager"
#define ZIGBEE_NVS_NAMESPACE "zigbee"
#define ZIGBEE_NVS_KEY_ACTIVE_PROFILE "active_profile"
#define ZIGBEE_NVS_KEY_TX_COUNTER "tx_counter"
#define ZIGBEE_NVS_KEY_ACTIVE_PAN "active_pan"
#define ZIGBEE_NVS_KEY_ACTIVE_CHANNEL "active_channel"
#define ZIGBEE_NVS_KEY_JOINED "joined"

typedef struct {
    bool initialized;
    bool stack_initialized;
    bool sd_available;
    bool joined;
    uint32_t active_profile_id;
    uint32_t tx_frame_counter;
    uint16_t active_pan_id;
    uint8_t active_channel;
    uint32_t next_profile_id;
    size_t profile_count;
    zigbee_profile_t profiles[ZIGBEE_MAX_PROFILES];
    SemaphoreHandle_t mutex;
} zigbee_manager_state_t;

static zigbee_manager_state_t s_state;

static void zigbee_manager_sanitize_name(char* out, size_t out_size, const char* in, const char* fallback) {
    size_t i;

    if(out == NULL || out_size == 0U) {
        return;
    }

    if(in == NULL || in[0] == '\0') {
        snprintf(out, out_size, "%s", fallback != NULL ? fallback : "Unnamed");
        return;
    }

    for(i = 0U; i + 1U < out_size && in[i] != '\0'; i++) {
        char ch = in[i];
        if(ch == '\t' || ch == '\n' || ch == '\r') {
            ch = ' ';
        }
        out[i] = ch;
    }
    out[i] = '\0';

    if(out[0] == '\0') {
        snprintf(out, out_size, "%s", fallback != NULL ? fallback : "Unnamed");
    }
}

static void zigbee_manager_seed_button_names(zigbee_profile_t* profile) {
    size_t i;

    if(profile == NULL) {
        return;
    }

    for(i = 0U; i < ZIGBEE_BUTTONS_PER_PROFILE; i++) {
        if(profile->buttons[i].name[0] == '\0') {
            snprintf(profile->buttons[i].name, sizeof(profile->buttons[i].name), "Button %u", (unsigned)(i + 1U));
        }
    }
}

static zigbee_profile_t* zigbee_manager_find_profile(uint32_t profile_id) {
    size_t i;

    for(i = 0U; i < s_state.profile_count; i++) {
        if(s_state.profiles[i].id == profile_id) {
            return &s_state.profiles[i];
        }
    }

    return NULL;
}

static void zigbee_manager_load_runtime_state_from_nvs(void) {
    nvs_handle_t nvs;
    uint32_t u32_value;
    uint8_t u8_value;

    if(nvs_open(ZIGBEE_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }

    if(nvs_get_u32(nvs, ZIGBEE_NVS_KEY_ACTIVE_PROFILE, &u32_value) == ESP_OK) {
        s_state.active_profile_id = u32_value;
    }

    if(nvs_get_u32(nvs, ZIGBEE_NVS_KEY_TX_COUNTER, &u32_value) == ESP_OK) {
        s_state.tx_frame_counter = u32_value;
    }

    if(nvs_get_u16(nvs, ZIGBEE_NVS_KEY_ACTIVE_PAN, &s_state.active_pan_id) != ESP_OK) {
        s_state.active_pan_id = 0U;
    }

    if(nvs_get_u8(nvs, ZIGBEE_NVS_KEY_ACTIVE_CHANNEL, &u8_value) == ESP_OK) {
        s_state.active_channel = u8_value;
    }

    if(nvs_get_u8(nvs, ZIGBEE_NVS_KEY_JOINED, &u8_value) == ESP_OK) {
        s_state.joined = (u8_value != 0U);
    }

    nvs_close(nvs);
}

static esp_err_t zigbee_manager_save_runtime_state_locked(void) {
    nvs_handle_t nvs;
    esp_err_t err;

    err = nvs_open(ZIGBEE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if(err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(nvs, ZIGBEE_NVS_KEY_ACTIVE_PROFILE, s_state.active_profile_id);
    if(err == ESP_OK) {
        err = nvs_set_u32(nvs, ZIGBEE_NVS_KEY_TX_COUNTER, s_state.tx_frame_counter);
    }
    if(err == ESP_OK) {
        err = nvs_set_u16(nvs, ZIGBEE_NVS_KEY_ACTIVE_PAN, s_state.active_pan_id);
    }
    if(err == ESP_OK) {
        err = nvs_set_u8(nvs, ZIGBEE_NVS_KEY_ACTIVE_CHANNEL, s_state.active_channel);
    }
    if(err == ESP_OK) {
        err = nvs_set_u8(nvs, ZIGBEE_NVS_KEY_JOINED, s_state.joined ? 1U : 0U);
    }
    if(err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

static esp_err_t zigbee_manager_save_profiles_locked(void) {
    if(!s_state.sd_available) {
        return ESP_ERR_INVALID_STATE;
    }

    return zigbee_profile_store_save(s_state.profiles, s_state.profile_count, s_state.next_profile_id);
}

static esp_err_t zigbee_manager_ensure_stack_initialized(void) {
    zigbee_stack_network_info_t net_info;
    size_t i;
    esp_err_t err;

    if(s_state.stack_initialized) {
        return ESP_OK;
    }

    err = zigbee_stack_init();
    if(err != ESP_OK) {
        ESP_LOGE(ZIGBEE_TAG, "zigbee stack init failed: 0x%X", (unsigned)err);
        return err;
    }

    s_state.stack_initialized = true;

    if(zigbee_stack_get_network_info(&net_info) == ESP_OK) {
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
        s_state.joined = net_info.joined;
        if(net_info.joined) {
            s_state.active_pan_id = net_info.pan_id;
            s_state.active_channel = net_info.channel;
        } else {
            s_state.active_profile_id = 0U;
            s_state.active_pan_id = 0U;
            s_state.active_channel = 0U;
            for(i = 0U; i < s_state.profile_count; i++) {
                s_state.profiles[i].joined = false;
            }
        }
        xSemaphoreGive(s_state.mutex);
    }

    return ESP_OK;
}

esp_err_t zigbee_manager_init(void) {
    esp_err_t err;
    size_t i;

    if(s_state.initialized) {
        return ESP_OK;
    }

    memset(&s_state, 0, sizeof(s_state));

    s_state.mutex = xSemaphoreCreateMutex();
    if(s_state.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if(err == ESP_OK) {
            err = nvs_flash_init();
        }
    }

    if(err != ESP_OK) {
        ESP_LOGE(ZIGBEE_TAG, "nvs init failed: 0x%X", (unsigned)err);
        return err;
    }

    err = zigbee_profile_store_load(
        s_state.profiles,
        ZIGBEE_MAX_PROFILES,
        &s_state.profile_count,
        &s_state.next_profile_id,
        &s_state.sd_available);
    if(err != ESP_OK) {
        ESP_LOGW(ZIGBEE_TAG, "profile load failed: 0x%X", (unsigned)err);
        s_state.profile_count = 0U;
        s_state.next_profile_id = 1U;
    }

    for(i = 0U; i < s_state.profile_count; i++) {
        zigbee_manager_seed_button_names(&s_state.profiles[i]);
    }

    zigbee_manager_load_runtime_state_from_nvs();

    s_state.initialized = true;
    ESP_LOGI(
        ZIGBEE_TAG,
        "initialized profiles=%u sd=%s",
        (unsigned)s_state.profile_count,
        s_state.sd_available ? "yes" : "no");

    return ESP_OK;
}

esp_err_t zigbee_manager_create_profile(const char* name, uint32_t* out_profile_id) {
    zigbee_profile_t* profile;
    esp_err_t err;

    if(!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if(out_profile_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    if(s_state.profile_count >= ZIGBEE_MAX_PROFILES) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NO_MEM;
    }

    profile = &s_state.profiles[s_state.profile_count];
    memset(profile, 0, sizeof(*profile));
    profile->id = s_state.next_profile_id++;
    zigbee_manager_sanitize_name(profile->name, sizeof(profile->name), name, "Profile");
    zigbee_manager_seed_button_names(profile);

    s_state.profile_count++;
    *out_profile_id = profile->id;

    err = zigbee_manager_save_profiles_locked();
    xSemaphoreGive(s_state.mutex);

    return err;
}

esp_err_t zigbee_manager_rename_profile(uint32_t profile_id, const char* name) {
    zigbee_profile_t* profile;
    esp_err_t err;

    if(!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if(name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    profile = zigbee_manager_find_profile(profile_id);
    if(profile == NULL) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    zigbee_manager_sanitize_name(profile->name, sizeof(profile->name), name, "Profile");
    err = zigbee_manager_save_profiles_locked();

    xSemaphoreGive(s_state.mutex);
    return err;
}

esp_err_t zigbee_manager_rename_button(uint32_t profile_id, uint8_t button_index, const char* name) {
    zigbee_profile_t* profile;
    esp_err_t err;

    if(!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if(name == NULL || button_index >= ZIGBEE_BUTTONS_PER_PROFILE) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    profile = zigbee_manager_find_profile(profile_id);
    if(profile == NULL) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    zigbee_manager_sanitize_name(
        profile->buttons[button_index].name,
        sizeof(profile->buttons[button_index].name),
        name,
        "Button");

    err = zigbee_manager_save_profiles_locked();

    xSemaphoreGive(s_state.mutex);
    return err;
}

esp_err_t zigbee_manager_delete_profile(uint32_t profile_id) {
    size_t i;
    esp_err_t err;

    if(!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    for(i = 0U; i < s_state.profile_count; i++) {
        if(s_state.profiles[i].id != profile_id) {
            continue;
        }

        if(i + 1U < s_state.profile_count) {
            memmove(&s_state.profiles[i], &s_state.profiles[i + 1U], (s_state.profile_count - i - 1U) * sizeof(s_state.profiles[0]));
        }

        s_state.profile_count--;

        if(s_state.active_profile_id == profile_id) {
            s_state.active_profile_id = 0U;
            s_state.joined = false;
            s_state.active_pan_id = 0U;
            s_state.active_channel = 0U;
            s_state.tx_frame_counter = 0U;
            zigbee_manager_save_runtime_state_locked();
        }

        err = zigbee_manager_save_profiles_locked();
        xSemaphoreGive(s_state.mutex);
        return err;
    }

    xSemaphoreGive(s_state.mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t zigbee_manager_join_profile(uint32_t profile_id, uint16_t pan_id, uint8_t channel) {
    size_t i;
    esp_err_t err;
    zigbee_stack_network_info_t net_info;

    /* saved state for rollback on failure */
    uint32_t prev_active_profile_id;
    uint16_t prev_active_pan_id;
    uint8_t prev_active_channel;
    bool prev_joined;

    if(!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if(channel != 0U && (channel < 11U || channel > 26U)) {
        return ESP_ERR_INVALID_ARG;
    }

    err = zigbee_manager_ensure_stack_initialized();
    if(err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    if(zigbee_manager_find_profile(profile_id) == NULL) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    /* save current state for rollback */
    prev_active_profile_id = s_state.active_profile_id;
    prev_active_pan_id     = s_state.active_pan_id;
    prev_active_channel    = s_state.active_channel;
    prev_joined            = s_state.joined;

    /* clear active state while joining - stack handles leave + reset internally */
    s_state.joined           = false;
    s_state.active_profile_id = 0U;
    s_state.active_pan_id    = 0U;
    s_state.active_channel   = 0U;

    /* release mutex before the long blocking call so STATUS queries can proceed */
    xSemaphoreGive(s_state.mutex);

    err = zigbee_stack_join(channel, 0U /* use default timeout */, &net_info);

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    if(err != ESP_OK) {
        /* restore previous state so STATUS still reports the last known profile */
        s_state.active_profile_id = prev_active_profile_id;
        s_state.active_pan_id     = prev_active_pan_id;
        s_state.active_channel    = prev_active_channel;
        s_state.joined            = prev_joined;
        xSemaphoreGive(s_state.mutex);
        return err;
    }

    for(i = 0U; i < s_state.profile_count; i++) {
        s_state.profiles[i].joined = false;
    }

    s_state.joined           = true;
    s_state.active_profile_id = profile_id;
    s_state.active_pan_id    = net_info.pan_id;
    s_state.active_channel   = net_info.channel;

    {
        zigbee_profile_t* profile = zigbee_manager_find_profile(profile_id);
        if(profile != NULL) {
            profile->joined  = true;
            profile->pan_id  = net_info.pan_id;
            profile->channel = net_info.channel;

            if(pan_id != 0U && pan_id != net_info.pan_id) {
                ESP_LOGW(
                    ZIGBEE_TAG,
                    "joined PAN differs from requested req=0x%04x actual=0x%04x",
                    (unsigned)pan_id,
                    (unsigned)net_info.pan_id);
            }
        }
    }

    err = zigbee_manager_save_runtime_state_locked();
    if(err == ESP_OK) {
        err = zigbee_manager_save_profiles_locked();
    }

    xSemaphoreGive(s_state.mutex);
    return err;
}

esp_err_t zigbee_manager_leave_network(void) {
    size_t i;
    esp_err_t err;

    if(!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    err = zigbee_manager_ensure_stack_initialized();
    if(err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    err = zigbee_stack_leave(15000U);
    if(err != ESP_OK) {
        xSemaphoreGive(s_state.mutex);
        return err;
    }

    s_state.joined = false;
    s_state.active_profile_id = 0U;
    s_state.active_pan_id = 0U;
    s_state.active_channel = 0U;

    for(i = 0U; i < s_state.profile_count; i++) {
        s_state.profiles[i].joined = false;
    }

    err = zigbee_manager_save_runtime_state_locked();
    if(err == ESP_OK) {
        err = zigbee_manager_save_profiles_locked();
    }

    xSemaphoreGive(s_state.mutex);
    return err;
}

esp_err_t zigbee_manager_trigger_button(uint32_t profile_id, uint8_t button_index, uint32_t* out_counter) {
    zigbee_profile_t* profile;
    esp_err_t err;

    if(!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if(out_counter == NULL || button_index >= ZIGBEE_BUTTONS_PER_PROFILE) {
        return ESP_ERR_INVALID_ARG;
    }

    err = zigbee_manager_ensure_stack_initialized();
    if(err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    profile = zigbee_manager_find_profile(profile_id);
    if(profile == NULL) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    if(!s_state.joined || s_state.active_profile_id != profile_id) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    err = zigbee_stack_trigger_button(button_index);
    if(err != ESP_OK) {
        xSemaphoreGive(s_state.mutex);
        return err;
    }

    s_state.tx_frame_counter++;
    *out_counter = s_state.tx_frame_counter;

    err = zigbee_manager_save_runtime_state_locked();

    ESP_LOGI(
        ZIGBEE_TAG,
        "trigger profile=%u button=%u name=%s counter=%u",
        (unsigned)profile_id,
        (unsigned)(button_index + 1U),
        profile->buttons[button_index].name,
        (unsigned)s_state.tx_frame_counter);

    xSemaphoreGive(s_state.mutex);
    return err;
}

esp_err_t zigbee_manager_get_status(zigbee_manager_status_t* out_status) {
    if(out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(!s_state.initialized) {
        memset(out_status, 0, sizeof(*out_status));
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    out_status->initialized = s_state.initialized;
    out_status->joined = s_state.joined;
    out_status->sd_available = s_state.sd_available;
    out_status->active_profile_id = s_state.active_profile_id;
    out_status->tx_frame_counter = s_state.tx_frame_counter;
    out_status->active_pan_id = s_state.active_pan_id;
    out_status->active_channel = s_state.active_channel;
    out_status->profile_count = s_state.profile_count;

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

esp_err_t zigbee_manager_list_profiles(const zigbee_profile_t** out_profiles, size_t* out_count) {
    if(out_profiles == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    *out_profiles = s_state.profiles;
    *out_count = s_state.profile_count;

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}
