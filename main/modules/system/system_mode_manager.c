#include "modules/system/system_mode_manager.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SYSTEM_MODE_TAG "system_mode"
#define SYSTEM_MODE_NVS_NAMESPACE "system_mode"
#define SYSTEM_MODE_NVS_KEY_ACTIVE "active"

typedef struct {
    bool initialized;
    system_mode_t active_mode;
} system_mode_state_t;

static system_mode_state_t s_state;

bool system_mode_manager_is_valid(system_mode_t mode) {
    return mode == SYSTEM_MODE_WIFI || mode == SYSTEM_MODE_ZIGBEE;
}

const char* system_mode_manager_mode_to_string(system_mode_t mode) {
    switch(mode) {
    case SYSTEM_MODE_WIFI:
        return "WIFI";
    case SYSTEM_MODE_ZIGBEE:
        return "ZIGBEE";
    default:
        return "UNKNOWN";
    }
}

static esp_err_t system_mode_manager_load_mode(system_mode_t* out_mode) {
    nvs_handle_t nvs;
    uint8_t raw_mode = (uint8_t)SYSTEM_MODE_WIFI;
    esp_err_t err;

    if(out_mode == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(SYSTEM_MODE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if(err != ESP_OK) {
        return err;
    }

    err = nvs_get_u8(nvs, SYSTEM_MODE_NVS_KEY_ACTIVE, &raw_mode);
    if(err == ESP_ERR_NVS_NOT_FOUND) {
        raw_mode = (uint8_t)SYSTEM_MODE_WIFI;
        err = nvs_set_u8(nvs, SYSTEM_MODE_NVS_KEY_ACTIVE, raw_mode);
        if(err == ESP_OK) {
            err = nvs_commit(nvs);
        }
    }

    if(err == ESP_OK && !system_mode_manager_is_valid((system_mode_t)raw_mode)) {
        raw_mode = (uint8_t)SYSTEM_MODE_WIFI;
        err = nvs_set_u8(nvs, SYSTEM_MODE_NVS_KEY_ACTIVE, raw_mode);
        if(err == ESP_OK) {
            err = nvs_commit(nvs);
        }
    }

    nvs_close(nvs);

    if(err != ESP_OK) {
        return err;
    }

    *out_mode = (system_mode_t)raw_mode;
    return ESP_OK;
}

esp_err_t system_mode_manager_init(void) {
    esp_err_t err;
    system_mode_t active_mode = SYSTEM_MODE_WIFI;

    if(s_state.initialized) {
        return ESP_OK;
    }

    err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if(err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if(err != ESP_OK) {
        return err;
    }

    err = system_mode_manager_load_mode(&active_mode);
    if(err != ESP_OK) {
        return err;
    }

    s_state.active_mode = active_mode;
    s_state.initialized = true;

    ESP_LOGI(
        SYSTEM_MODE_TAG,
        "initialized active_mode=%s",
        system_mode_manager_mode_to_string(active_mode));

    return ESP_OK;
}

esp_err_t system_mode_manager_get_active_mode(system_mode_t* out_mode) {
    if(out_mode == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_mode = s_state.active_mode;
    return ESP_OK;
}

esp_err_t system_mode_manager_set_active_mode(system_mode_t mode) {
    nvs_handle_t nvs;
    esp_err_t err;

    if(!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if(!system_mode_manager_is_valid(mode)) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(SYSTEM_MODE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if(err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(nvs, SYSTEM_MODE_NVS_KEY_ACTIVE, (uint8_t)mode);
    if(err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);

    if(err != ESP_OK) {
        return err;
    }

    s_state.active_mode = mode;
    ESP_LOGI(SYSTEM_MODE_TAG, "active_mode=%s", system_mode_manager_mode_to_string(mode));

    return ESP_OK;
}
