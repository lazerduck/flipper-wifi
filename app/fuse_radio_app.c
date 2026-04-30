#include "fuse_radio_app_i.h"

#include <gui/elements.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define FUSE_RADIO_RX_STREAM_SIZE 1024U
#define FUSE_RADIO_DATA_DIR APP_DATA_PATH("fuse_radio")
#define FUSE_RADIO_CREDENTIALS_PATH APP_DATA_PATH("fuse_radio/credentials.fff")
#define FUSE_RADIO_CREDENTIALS_FILETYPE "Fuse Radio Credentials"
#define FUSE_RADIO_CREDENTIALS_VERSION 1U
#define FUSE_RADIO_BLE_DEVICES_PATH APP_DATA_PATH("fuse_radio/ble_devices.fff")
#define FUSE_RADIO_BLE_DEVICES_FILETYPE "Fuse Radio BLE Devices"
#define FUSE_RADIO_BLE_DEVICES_VERSION 2U
#define FUSE_RADIO_BLE_SCAN_NORMAL_DURATION_MS 5000U
#define FUSE_RADIO_BLE_SCAN_DEEP_DURATION_MS   30000U
#define FUSE_RADIO_BLE_DISTANCE_INTERVAL_MS     500U

static size_t fuse_radio_app_strlcpy(char* dst, const char* src, size_t size) {
    const size_t length = strlen(src);

    if(size != 0U) {
        size_t copy_length = length;
        if(copy_length >= size) {
            copy_length = size - 1U;
        }

        memcpy(dst, src, copy_length);
        dst[copy_length] = '\0';
    }

    return length;
}

static int32_t fuse_radio_app_find_saved_credential_index(FuseRadioApp* app, const char* ssid) {
    for(uint8_t index = 0U; index < app->saved_credential_count; index++) {
        if(strcmp(app->saved_credentials[index].ssid, ssid) == 0) {
            return (int32_t)index;
        }
    }

    return -1;
}

static int32_t fuse_radio_app_find_saved_ble_device_index(FuseRadioApp* app, const char* mac) {
    for(uint8_t index = 0U; index < app->saved_ble_results.count; index++) {
        if(strcmp(app->saved_ble_results.devices[index].mac, mac) == 0) {
            return (int32_t)index;
        }
    }

    return -1;
}

static uint16_t fuse_radio_app_ble_scan_duration_ms(FuseRadioApp* app) {
    return app->ble_scan_mode == FuseRadioBleScanModeDeep ? FUSE_RADIO_BLE_SCAN_DEEP_DURATION_MS :
                                                            FUSE_RADIO_BLE_SCAN_NORMAL_DURATION_MS;
}

static void fuse_radio_app_reset_led_state(FuseRadioApp* app) {
    app->led_red = 3U;
    app->led_green = 3U;
    app->led_blue = 3U;
    app->led_manual_override = false;
    app->led_edit_channel = FuseRadioLedChannelRed;
}

static void fuse_radio_app_reset_ble_distance_state(FuseRadioApp* app) {
    app->ble_distance_rssi = -127;
    app->ble_distance_distance_dm = -1;
    app->ble_distance_samples = 0U;
    app->ble_distance_missed_scans = 0U;
    app->ble_distance_started_at = 0U;
    app->ble_distance_active = false;
    app->ble_distance_stop_pending = false;
    app->ble_distance_seen = false;
    app->ble_distance_has_error = false;
    fuse_radio_app_strlcpy(app->ble_distance_trend, "steady", sizeof(app->ble_distance_trend));
    app->ble_distance_error[0] = '\0';
}

static bool fuse_radio_app_save_credentials(FuseRadioApp* app) {
    if(!app->storage || !app->credentials_format) {
        return false;
    }

    if(!storage_dir_exists(app->storage, FUSE_RADIO_DATA_DIR) &&
       !storage_simply_mkdir(app->storage, FUSE_RADIO_DATA_DIR)) {
        return false;
    }

    if(!flipper_format_file_open_always(app->credentials_format, FUSE_RADIO_CREDENTIALS_PATH)) {
        return false;
    }

    bool success = true;
    do {
        if(!flipper_format_write_header_cstr(
               app->credentials_format,
               FUSE_RADIO_CREDENTIALS_FILETYPE,
               FUSE_RADIO_CREDENTIALS_VERSION)) {
            success = false;
            break;
        }

        for(uint8_t index = 0U; index < app->saved_credential_count; index++) {
            const FuseRadioSavedCredential* credential = &app->saved_credentials[index];
            if(!flipper_format_write_string_cstr(app->credentials_format, "SSID", credential->ssid) ||
               !flipper_format_write_string_cstr(
                   app->credentials_format, "Password", credential->password)) {
                success = false;
                break;
            }

            flipper_format_write_empty_line(app->credentials_format);
        }
    } while(false);

    flipper_format_file_close(app->credentials_format);
    return success;
}

static bool fuse_radio_app_save_ble_devices(FuseRadioApp* app) {
    if(!app->storage || !app->credentials_format) {
        return false;
    }

    if(!storage_dir_exists(app->storage, FUSE_RADIO_DATA_DIR) &&
       !storage_simply_mkdir(app->storage, FUSE_RADIO_DATA_DIR)) {
        return false;
    }

    if(!flipper_format_file_open_always(app->credentials_format, FUSE_RADIO_BLE_DEVICES_PATH)) {
        return false;
    }

    bool success = true;
    do {
        if(!flipper_format_write_header_cstr(
               app->credentials_format,
               FUSE_RADIO_BLE_DEVICES_FILETYPE,
               FUSE_RADIO_BLE_DEVICES_VERSION)) {
            success = false;
            break;
        }

        for(uint8_t index = 0U; index < app->saved_ble_results.count; index++) {
            const FuseRadioSavedBleDevice* device = &app->saved_ble_results.devices[index];
            const char* name = device->has_name ? device->name : "-";
            const char* company = device->has_company ? device->company : "-";
            const char* appearance = device->has_appearance ? device->appearance : "-";
            const char* connectable = device->connectable ? "YES" : "NO";

            if(!flipper_format_write_string_cstr(app->credentials_format, "MAC", device->mac) ||
               !flipper_format_write_string_cstr(app->credentials_format, "Name", name) ||
               !flipper_format_write_string_cstr(
                   app->credentials_format, "Company", company) ||
               !flipper_format_write_string_cstr(
                   app->credentials_format, "Appearance", appearance) ||
               !flipper_format_write_string_cstr(
                   app->credentials_format, "Class", device->classification) ||
               !flipper_format_write_string_cstr(
                   app->credentials_format, "Proximity", device->proximity) ||
               !flipper_format_write_string_cstr(
                   app->credentials_format, "AddrType", device->addr_type) ||
               !flipper_format_write_string_cstr(
                   app->credentials_format, "Connectable", connectable)) {
                success = false;
                break;
            }

            flipper_format_write_empty_line(app->credentials_format);
        }
    } while(false);

    flipper_format_file_close(app->credentials_format);
    return success;
}

static void fuse_radio_app_load_credentials(FuseRadioApp* app) {
    app->saved_credential_count = 0U;

    if(!app->storage || !app->credentials_format) {
        return;
    }

    if(!storage_file_exists(app->storage, FUSE_RADIO_CREDENTIALS_PATH)) {
        return;
    }

    if(!flipper_format_file_open_existing(app->credentials_format, FUSE_RADIO_CREDENTIALS_PATH)) {
        return;
    }

    FuriString* filetype = furi_string_alloc();
    FuriString* ssid = furi_string_alloc();
    FuriString* password = furi_string_alloc();
    uint32_t version = 0U;

    do {
        if(!flipper_format_read_header(app->credentials_format, filetype, &version)) {
            break;
        }

        if((strcmp(furi_string_get_cstr(filetype), FUSE_RADIO_CREDENTIALS_FILETYPE) != 0) ||
           (version != FUSE_RADIO_CREDENTIALS_VERSION)) {
            break;
        }

        while(app->saved_credential_count < FUSE_RADIO_MAX_SAVED_NETWORKS) {
            if(!flipper_format_read_string(app->credentials_format, "SSID", ssid)) {
                break;
            }

            if(!flipper_format_read_string(app->credentials_format, "Password", password)) {
                break;
            }

            FuseRadioSavedCredential* credential =
                &app->saved_credentials[app->saved_credential_count++];
            fuse_radio_app_strlcpy(
                credential->ssid, furi_string_get_cstr(ssid), sizeof(credential->ssid));
            fuse_radio_app_strlcpy(
                credential->password,
                furi_string_get_cstr(password),
                sizeof(credential->password));
        }
    } while(false);

    furi_string_free(password);
    furi_string_free(ssid);
    furi_string_free(filetype);
    flipper_format_file_close(app->credentials_format);
}

static void fuse_radio_app_load_ble_devices(FuseRadioApp* app) {
    app->saved_ble_results.count = 0U;

    if(!app->storage || !app->credentials_format) {
        return;
    }

    if(!storage_file_exists(app->storage, FUSE_RADIO_BLE_DEVICES_PATH)) {
        return;
    }

    if(!flipper_format_file_open_existing(app->credentials_format, FUSE_RADIO_BLE_DEVICES_PATH)) {
        return;
    }

    FuriString* filetype = furi_string_alloc();
    FuriString* mac = furi_string_alloc();
    FuriString* name = furi_string_alloc();
    FuriString* company = furi_string_alloc();
    FuriString* appearance = furi_string_alloc();
    FuriString* classification = furi_string_alloc();
    FuriString* proximity = furi_string_alloc();
    FuriString* addr_type = furi_string_alloc();
    FuriString* connectable = furi_string_alloc();
    uint32_t version = 0U;

    do {
        if(!flipper_format_read_header(app->credentials_format, filetype, &version)) {
            break;
        }

          if((strcmp(furi_string_get_cstr(filetype), FUSE_RADIO_BLE_DEVICES_FILETYPE) != 0) ||
              (version != 1U && version != FUSE_RADIO_BLE_DEVICES_VERSION)) {
            break;
        }

        while(app->saved_ble_results.count < FUSE_RADIO_MAX_SAVED_BLE_DEVICES) {
            if(!flipper_format_read_string(app->credentials_format, "MAC", mac)) {
                break;
            }

            if(!flipper_format_read_string(app->credentials_format, "Name", name)) {
                break;
            }

            if(version >= FUSE_RADIO_BLE_DEVICES_VERSION) {
                if(!flipper_format_read_string(app->credentials_format, "Company", company) ||
                   !flipper_format_read_string(app->credentials_format, "Appearance", appearance) ||
                   !flipper_format_read_string(
                       app->credentials_format, "Class", classification) ||
                   !flipper_format_read_string(
                       app->credentials_format, "Proximity", proximity)) {
                    break;
                }
            }

            if(!flipper_format_read_string(app->credentials_format, "AddrType", addr_type) ||
               !flipper_format_read_string(app->credentials_format, "Connectable", connectable)) {
                break;
            }

            FuseRadioSavedBleDevice* device =
                &app->saved_ble_results.devices[app->saved_ble_results.count++];
            memset(device, 0, sizeof(*device));
            fuse_radio_app_strlcpy(device->mac, furi_string_get_cstr(mac), sizeof(device->mac));
            fuse_radio_app_strlcpy(
                device->addr_type, furi_string_get_cstr(addr_type), sizeof(device->addr_type));
            device->connectable = strcmp(furi_string_get_cstr(connectable), "YES") == 0;
            device->has_name = strcmp(furi_string_get_cstr(name), "-") != 0;
            if(device->has_name) {
                fuse_radio_app_strlcpy(device->name, furi_string_get_cstr(name), sizeof(device->name));
            }
            if(version >= FUSE_RADIO_BLE_DEVICES_VERSION) {
                device->has_company = strcmp(furi_string_get_cstr(company), "-") != 0;
                if(device->has_company) {
                    fuse_radio_app_strlcpy(
                        device->company,
                        furi_string_get_cstr(company),
                        sizeof(device->company));
                }
                device->has_appearance = strcmp(furi_string_get_cstr(appearance), "-") != 0;
                if(device->has_appearance) {
                    fuse_radio_app_strlcpy(
                        device->appearance,
                        furi_string_get_cstr(appearance),
                        sizeof(device->appearance));
                }
                fuse_radio_app_strlcpy(
                    device->classification,
                    furi_string_get_cstr(classification),
                    sizeof(device->classification));
                fuse_radio_app_strlcpy(
                    device->proximity,
                    furi_string_get_cstr(proximity),
                    sizeof(device->proximity));
            }
            device->last_rssi = -127;
            device->seen_recently = false;
        }
    } while(false);

    furi_string_free(connectable);
    furi_string_free(addr_type);
    furi_string_free(proximity);
    furi_string_free(classification);
    furi_string_free(appearance);
    furi_string_free(company);
    furi_string_free(name);
    furi_string_free(mac);
    furi_string_free(filetype);
    flipper_format_file_close(app->credentials_format);
}

static void fuse_radio_app_clear_saved_ble_presence(FuseRadioApp* app) {
    for(uint8_t index = 0U; index < app->saved_ble_results.count; index++) {
        app->saved_ble_results.devices[index].seen_recently = false;
        app->saved_ble_results.devices[index].last_rssi = -127;
    }
}

static void fuse_radio_app_touch_saved_ble_device(FuseRadioApp* app, const FuseRadioBleDevice* device) {
    const int32_t index = fuse_radio_app_find_saved_ble_device_index(app, device->mac);
    if(index < 0) {
        return;
    }

    FuseRadioSavedBleDevice* saved = &app->saved_ble_results.devices[index];
    fuse_radio_app_strlcpy(saved->addr_type, device->addr_type, sizeof(saved->addr_type));
    saved->connectable = device->connectable;
    saved->has_name = device->has_name;
    saved->has_company = device->has_company;
    saved->has_appearance = device->has_appearance;
    if(device->has_name) {
        fuse_radio_app_strlcpy(saved->name, device->name, sizeof(saved->name));
    } else {
        saved->name[0] = '\0';
    }
    if(device->has_company) {
        fuse_radio_app_strlcpy(saved->company, device->company, sizeof(saved->company));
    } else {
        saved->company[0] = '\0';
    }
    if(device->has_appearance) {
        fuse_radio_app_strlcpy(saved->appearance, device->appearance, sizeof(saved->appearance));
    } else {
        saved->appearance[0] = '\0';
    }
    fuse_radio_app_strlcpy(saved->classification, device->classification, sizeof(saved->classification));
    fuse_radio_app_strlcpy(saved->proximity, device->proximity, sizeof(saved->proximity));
    saved->last_rssi = device->rssi;
    saved->seen_recently = true;
}

const char* fuse_radio_app_get_saved_password(FuseRadioApp* app, const char* ssid) {
    const int32_t index = fuse_radio_app_find_saved_credential_index(app, ssid);
    if(index < 0) {
        return NULL;
    }

    return app->saved_credentials[index].password;
}

bool fuse_radio_app_store_saved_credential(FuseRadioApp* app, const char* ssid, const char* password) {
    if(!ssid[0] || !password[0]) {
        return false;
    }

    int32_t index = fuse_radio_app_find_saved_credential_index(app, ssid);
    if(index < 0) {
        if(app->saved_credential_count < FUSE_RADIO_MAX_SAVED_NETWORKS) {
            index = (int32_t)app->saved_credential_count++;
        } else {
            memmove(
                &app->saved_credentials[0],
                &app->saved_credentials[1],
                sizeof(app->saved_credentials[0]) * (FUSE_RADIO_MAX_SAVED_NETWORKS - 1U));
            index = FUSE_RADIO_MAX_SAVED_NETWORKS - 1;
        }
    }

    FuseRadioSavedCredential* credential = &app->saved_credentials[index];
    fuse_radio_app_strlcpy(credential->ssid, ssid, sizeof(credential->ssid));
    fuse_radio_app_strlcpy(credential->password, password, sizeof(credential->password));

    return fuse_radio_app_save_credentials(app);
}

static bool fuse_radio_app_custom_event_callback(void* context, uint32_t event) {
    FuseRadioApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

typedef struct {
    FuseRadioSurveyPreset preset;
    FuseRadioPromiscuousPreset repeat_preset;
    const char* command;
    uint8_t channels[FUSE_RADIO_MAX_SURVEY_CHANNELS];
    uint8_t channel_count;
    uint16_t dwell_ms;
} FuseRadioSurveyPresetConfig;

static const FuseRadioSurveyPresetConfig fuse_radio_app_survey_presets[] = {
    {.preset = FuseRadioSurveyPreset11611,
     .repeat_preset = FuseRadioPromiscuousPresetSurvey11611,
    .command = "WIFI PROMISCUOUS SURVEY channels=1,6,11 dwell_ms=500\n",
     .channels = {1U, 6U, 11U},
     .channel_count = 3U,
    .dwell_ms = 500U},
    {.preset = FuseRadioSurveyPreset111,
     .repeat_preset = FuseRadioPromiscuousPresetSurvey111,
        .command = "WIFI PROMISCUOUS SURVEY dwell_ms=500\n",
     .channels = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U},
     .channel_count = 11U,
    .dwell_ms = 500U},
    {.preset = FuseRadioSurveyPreset14814,
     .repeat_preset = FuseRadioPromiscuousPresetSurvey14814,
    .command = "WIFI PROMISCUOUS SURVEY channels=1,4,8,14 dwell_ms=500\n",
     .channels = {1U, 4U, 8U, 14U},
     .channel_count = 4U,
    .dwell_ms = 500U},
    {.preset = FuseRadioSurveyPreset1357911,
     .repeat_preset = FuseRadioPromiscuousPresetSurvey1357911,
    .command = "WIFI PROMISCUOUS SURVEY channels=1,3,5,7,9,11 dwell_ms=500\n",
     .channels = {1U, 3U, 5U, 7U, 9U, 11U},
     .channel_count = 6U,
    .dwell_ms = 500U},
};

static const FuseRadioSurveyPresetConfig*
    fuse_radio_app_get_survey_preset_config(FuseRadioSurveyPreset preset) {
    for(size_t index = 0U; index < COUNT_OF(fuse_radio_app_survey_presets); index++) {
        if(fuse_radio_app_survey_presets[index].preset == preset) {
            return &fuse_radio_app_survey_presets[index];
        }
    }

    return &fuse_radio_app_survey_presets[0];
}

static bool fuse_radio_app_back_event_callback(void* context) {
    FuseRadioApp* app = context;

    if(scene_manager_handle_back_event(app->scene_manager)) {
        return true;
    }

    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static void fuse_radio_app_scan_view_callback(FuseRadioScanViewAction action, void* context) {
    FuseRadioApp* app = context;

    if(action == FuseRadioScanViewActionRescan) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventScanRescan);
    } else if(action == FuseRadioScanViewActionSelect) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventScanSelect);
    }
}

static void fuse_radio_app_ble_scan_view_callback(
    FuseRadioBleScanViewAction action,
    void* context) {
    FuseRadioApp* app = context;

    if(action == FuseRadioBleScanViewActionRefresh) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventBleScanRefresh);
    } else if(action == FuseRadioBleScanViewActionSelect) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventBleSelectDevice);
    }
}

static void fuse_radio_app_watch_result_view_callback(
    FuseRadioWatchResultViewAction action,
    void* context) {
    FuseRadioApp* app = context;

    if(action == FuseRadioWatchResultViewActionRepeat) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiPromiscuousRepeat);
    }
}

static void fuse_radio_app_survey_preset_view_callback(FuseRadioSurveyPreset preset, void* context) {
    FuseRadioApp* app = context;

    app->survey_results.preset = preset;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, FuseRadioCustomEventWifiSurveyPresetStart);
}

static void fuse_radio_app_survey_result_view_callback(
    FuseRadioSurveyResultViewAction action,
    void* context) {
    FuseRadioApp* app = context;

    if(action == FuseRadioSurveyResultViewActionRepeat) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiPromiscuousRepeat);
    }
}

static void fuse_radio_app_discover_result_view_callback(
    FuseRadioDiscoverResultViewAction action,
    void* context) {
    FuseRadioApp* app = context;

    if(action == FuseRadioDiscoverResultViewActionRepeat) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiDiscoverRefresh);
    }
}

static void fuse_radio_app_watch_channel_callback(uint8_t channel, void* context) {
    FuseRadioApp* app = context;
    app->promiscuous_watch_channel = channel;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, FuseRadioCustomEventWifiWatchChannelStart);
}

static void fuse_radio_app_led_value_callback(uint8_t value, void* context) {
    FuseRadioApp* app = context;

    switch(app->led_edit_channel) {
    case FuseRadioLedChannelRed:
        app->led_red = value;
        break;
    case FuseRadioLedChannelGreen:
        app->led_green = value;
        break;
    case FuseRadioLedChannelBlue:
    default:
        app->led_blue = value;
        break;
    }

    app->led_manual_override = true;
    view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventLedValueSet);
}

static void fuse_radio_app_watch_live_view_callback(void* context) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventWifiPromiscuousStop);
}

static void fuse_radio_app_ble_distance_button_callback(
    GuiButtonType button,
    InputType type,
    void* context) {
    FuseRadioApp* app = context;

    if(type != InputTypeShort) {
        return;
    }

    if(button == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventBleDistanceStop);
    } else if(button == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventBleDistanceBack);
    }
}

static void fuse_radio_app_sd_widget_button_callback(
    GuiButtonType button,
    InputType type,
    void* context) {
    FuseRadioApp* app = context;

    if(type != InputTypeShort) {
        return;
    }

    if(button == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventSdRefresh);
    } else if(button == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventSdFormatConfirm);
    }
}

static void fuse_radio_app_sd_explore_submenu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_app_text_input_callback(void* context) {
    FuseRadioApp* app = context;

    if(app->text_input_mode == FuseRadioTextInputConnectSsid) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventConnectSsidDone);
    } else if(app->text_input_mode == FuseRadioTextInputConnectPassword) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventConnectPasswordDone);
    } else if(app->text_input_mode == FuseRadioTextInputMdnsHost) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventMdnsHostDone);
    }
}

static void fuse_radio_app_startup_view_callback(void* context) {
    FuseRadioApp* app = context;

    if(app->module_state == FuseRadioModuleStateError) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventRetryDetection);
    } else if(app->module_state == FuseRadioModuleStateDetected) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventModuleDetected);
    }
}

static void fuse_radio_app_wifi_status_button_callback(
    GuiButtonType button,
    InputType type,
    void* context) {
    FuseRadioApp* app = context;

    if(type != InputTypeShort) {
        return;
    }

    if(button == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiStatusRefresh);
    } else if(button == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventWifiStatusMenu);
    }
}

static void fuse_radio_app_wifi_promiscuous_button_callback(
    GuiButtonType button,
    InputType type,
    void* context) {
    FuseRadioApp* app = context;

    if(type != InputTypeShort) {
        return;
    }

    if(button == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher,
            app->promiscuous_watch_live_active ? FuseRadioCustomEventWifiPromiscuousStop :
                                                 FuseRadioCustomEventWifiPromiscuousRepeat);
    } else if(button == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiPromiscuousMenu);
    }
}

static void
    fuse_radio_app_wifi_mdns_button_callback(GuiButtonType button, InputType type, void* context) {
    FuseRadioApp* app = context;

    if(button == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiMdnsRefresh);
    }
}

static void fuse_radio_app_wifi_http_button_callback(
    GuiButtonType button,
    InputType type,
    void* context) {
    FuseRadioApp* app = context;

    if(button == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiHttpRefresh);
    }
}

static void fuse_radio_app_gatt_services_submenu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    app->ble_gatt_selected_svc = (uint8_t)index;
    view_dispatcher_send_custom_event(app->view_dispatcher, (uint32_t)index);
}

static void fuse_radio_app_tick_event_callback(void* context) {
    FuseRadioApp* app = context;
    fuse_radio_app_handle_tick(app);
    scene_manager_handle_tick_event(app->scene_manager);
}

static void fuse_radio_app_serial_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    FuseRadioApp* app = context;
    UNUSED(handle);

    if(event != FuriHalSerialRxEventData) {
        return;
    }

    const uint8_t byte = furi_hal_serial_async_rx(app->serial_handle);
    furi_stream_buffer_send(app->rx_stream, &byte, sizeof(byte), 0U);
}

static void fuse_radio_app_reset_scan_results(FuseRadioApp* app) {
    memset(&app->scan_results, 0, sizeof(app->scan_results));
    app->scan_dirty = true;
}

static void fuse_radio_app_reset_ble_scan_results(FuseRadioApp* app) {
    memset(&app->ble_scan_results, 0, sizeof(app->ble_scan_results));
    app->ble_scan_results.scan_duration_ms = fuse_radio_app_ble_scan_duration_ms(app);
    app->ble_info_text[0] = '\0';
    app->ble_scan_started_at = 0U;
    app->ble_dirty = true;
}

static void fuse_radio_app_reset_mdns_results(FuseRadioApp* app) {
    app->mdns_info_text[0] = '\0';
    app->mdns_count = 0U;
    app->mdns_truncated_count = 0U;
    app->mdns_dirty = true;
}

static void fuse_radio_app_reset_http_results(FuseRadioApp* app) {
    app->http_info_text[0] = '\0';
    app->http_dirty = true;
}

static void fuse_radio_app_reset_promiscuous_results(FuseRadioApp* app) {
    app->promiscuous_info_text[0] = '\0';
    app->promiscuous_live_text[0] = '\0';
    memset(&app->survey_results, 0, sizeof(app->survey_results));
    memset(&app->watch_summary, 0, sizeof(app->watch_summary));
    memset(app->watch_devices, 0, sizeof(app->watch_devices));
    app->promiscuous_live_elapsed_ms = 0U;
    app->promiscuous_live_total_frames = 0U;
    app->promiscuous_live_unique_count = 0U;
    app->promiscuous_live_beacon_count = 0U;
    app->promiscuous_live_max_rssi = -127;
    app->watch_device_count = 0U;
    app->promiscuous_watch_live_active = false;
    app->promiscuous_watch_stop_pending = false;
    app->promiscuous_dirty = true;
}

static void fuse_radio_app_prepare_survey_state(FuseRadioApp* app, FuseRadioSurveyPreset preset) {
    const FuseRadioSurveyPresetConfig* config = fuse_radio_app_get_survey_preset_config(preset);

    memset(&app->survey_results, 0, sizeof(app->survey_results));
    app->survey_results.preset = preset;
    app->survey_results.expected_count = config->channel_count;
    app->survey_results.current_channel = config->channels[0];
    app->survey_results.dwell_ms = config->dwell_ms;
    app->survey_results.active = true;

    for(uint8_t index = 0U; index < config->channel_count; index++) {
        app->survey_results.channels[index].channel = config->channels[index];
    }
}

static void fuse_radio_app_set_promiscuous_live_text(FuseRadioApp* app, const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vsnprintf(app->promiscuous_live_text, sizeof(app->promiscuous_live_text), fmt, args);
    va_end(args);
    app->promiscuous_dirty = true;
}
static void fuse_radio_app_refresh_promiscuous_live_view(FuseRadioApp* app) {
    FuseRadioWatchLiveSnapshot snapshot = {
        .channel = app->promiscuous_watch_channel,
        .elapsed_ms = app->promiscuous_live_elapsed_ms,
        .total_frames = app->promiscuous_live_total_frames,
        .unique_count = app->promiscuous_live_unique_count,
        .beacon_count = app->promiscuous_live_beacon_count,
        .max_rssi = app->promiscuous_live_max_rssi,
        .stop_pending = app->promiscuous_watch_stop_pending,
    };

    fuse_radio_watch_live_view_set_snapshot(app->watch_live_view, &snapshot);
    app->promiscuous_dirty = false;
}

static void fuse_radio_app_update_watch_device_roles(FuseRadioApp* app) {
    uint8_t best_index = 0U;
    bool found = false;

    for(uint8_t index = 0U; index < app->watch_device_count; index++) {
        app->watch_devices[index].likely_ap = false;
    }

    for(uint8_t index = 0U; index < app->watch_device_count; index++) {
        if(!found || app->watch_devices[index].beacons > app->watch_devices[best_index].beacons ||
           (app->watch_devices[index].beacons == app->watch_devices[best_index].beacons &&
            app->watch_devices[index].frames > app->watch_devices[best_index].frames)) {
            best_index = index;
            found = true;
        }
    }

    if(found) {
        app->watch_devices[best_index].likely_ap = true;
    }
}

static void fuse_radio_app_refresh_watch_result_view(FuseRadioApp* app) {
    fuse_radio_watch_result_view_set_data(
        app->watch_result_view, &app->watch_summary, app->watch_devices, app->watch_device_count);
    app->promiscuous_dirty = false;
}

static void fuse_radio_app_refresh_survey_progress_view(FuseRadioApp* app) {
    FuseRadioSurveyProgressSnapshot snapshot = {
        .current_channel = app->survey_results.current_channel,
        .completed_count = app->survey_results.completed_count,
        .total_count = app->survey_results.expected_count,
        .progress_percent = app->survey_results.progress_percent,
        .animation_frame = 0U,
    };

    fuse_radio_survey_progress_view_set_snapshot(app->survey_progress_view, &snapshot);
    app->promiscuous_dirty = false;
}

static void fuse_radio_app_refresh_survey_result_view(FuseRadioApp* app) {
    fuse_radio_survey_result_view_set_data(app->survey_result_view, &app->survey_results);
    app->promiscuous_dirty = false;
}

static void fuse_radio_app_reset_discover_results(FuseRadioApp* app) {
    memset(&app->discover_results, 0, sizeof(app->discover_results));
    app->discover_dirty = true;
}

static void fuse_radio_app_update_ble_scan_progress(FuseRadioApp* app) {
    if(!app->ble_scan_results.active || app->ble_scan_results.scan_duration_ms == 0U ||
       app->ble_scan_started_at == 0U) {
        return;
    }

    const uint32_t elapsed_ms = furi_get_tick() - app->ble_scan_started_at;
    uint8_t progress = 99U;

    if(elapsed_ms < app->ble_scan_results.scan_duration_ms) {
        progress = (uint8_t)((elapsed_ms * 100U) / app->ble_scan_results.scan_duration_ms);
        if(progress >= 100U) {
            progress = 99U;
        }
    }

    if(progress != app->ble_scan_results.progress_percent) {
        app->ble_scan_results.progress_percent = progress;
        app->ble_dirty = true;
    }
}

static void fuse_radio_app_set_wifi_info_text(FuseRadioApp* app, const char* text) {
    strncpy(app->wifi_info_text, text, sizeof(app->wifi_info_text) - 1U);
    app->wifi_info_text[sizeof(app->wifi_info_text) - 1U] = '\0';
    app->wifi_info_dirty = true;
}

void fuse_radio_app_refresh_ble_scan_view(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_ble_scan_view_set_mode(app->ble_scan_view, FuseRadioBleScanViewModeScan);
    fuse_radio_ble_scan_view_set_scan_data(app->ble_scan_view, &app->ble_scan_results);
    app->ble_dirty = false;
}

void fuse_radio_app_refresh_saved_ble_view(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_ble_scan_view_set_mode(app->ble_scan_view, FuseRadioBleScanViewModeSaved);
    fuse_radio_ble_scan_view_set_saved_data(app->ble_scan_view, &app->saved_ble_results);
    app->ble_dirty = false;
}

void fuse_radio_app_refresh_ble_distance_widget(FuseRadioApp* app) {
    furi_assert(app);

    char title[24];
    char detail[256];
    char trend[16];
    char rssi_text[16];
    char distance_text[20];
    char signal[8];
    uint8_t bars = 0U;

    if(app->ble_distance_rssi >= -55) {
        bars = 4U;
    } else if(app->ble_distance_rssi >= -67) {
        bars = 3U;
    } else if(app->ble_distance_rssi >= -78) {
        bars = 2U;
    } else if(app->ble_distance_rssi >= -90) {
        bars = 1U;
    }

    for(uint8_t index = 0U; index < 4U; index++) {
        signal[index] = index < bars ? '#' : '-';
    }
    signal[4] = '\0';

    if(app->ble_distance_seen) {
        snprintf(rssi_text, sizeof(rssi_text), "%d dBm", app->ble_distance_rssi);
        if(app->ble_distance_distance_dm >= 0) {
            snprintf(
                distance_text,
                sizeof(distance_text),
                "%lu.%lum",
                (unsigned long)(app->ble_distance_distance_dm / 10),
                (unsigned long)(app->ble_distance_distance_dm % 10));
        } else {
            snprintf(distance_text, sizeof(distance_text), "--");
        }
    } else {
        snprintf(rssi_text, sizeof(rssi_text), "not seen");
        snprintf(distance_text, sizeof(distance_text), "--");
    }

    widget_reset(app->widget);

    snprintf(
        title,
        sizeof(title),
        app->ble_selection.device.has_name ? "Dist %s" : "BLE Distance",
        app->ble_selection.device.has_name ? app->ble_selection.device.name : "");
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, title);

    fuse_radio_app_strlcpy(trend, app->ble_distance_trend, sizeof(trend));
    for(size_t index = 0U; trend[index] != '\0'; index++) {
        trend[index] = (char)tolower((uint8_t)trend[index]);
    }

    if(app->ble_distance_has_error && app->ble_distance_error[0]) {
        snprintf(
            detail,
            sizeof(detail),
            "MAC %s\nError\n%s\n\nCenter: restart",
            app->ble_selection.device.mac,
            app->ble_distance_error);
    } else {
        snprintf(
            detail,
            sizeof(detail),
            "MAC %s\nRSSI %s  [%s]\nTrend %s\nRough %s\nSamples %u  Miss %u",
            app->ble_selection.device.mac,
            rssi_text,
            signal,
            trend,
            distance_text,
            (unsigned)app->ble_distance_samples,
            (unsigned)app->ble_distance_missed_scans);
    }

    widget_add_text_scroll_element(app->widget, 0, 14, 128, 38, detail);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeLeft,
        "Back",
        fuse_radio_app_ble_distance_button_callback,
        app);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        app->ble_distance_active ? (app->ble_distance_stop_pending ? "Wait" : "Stop") : "Start",
        fuse_radio_app_ble_distance_button_callback,
        app);

    app->ble_dirty = false;
}

void fuse_radio_app_refresh_ble_info_widget(FuseRadioApp* app) {
    furi_assert(app);

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget,
        64,
        6,
        AlignCenter,
        AlignTop,
        FontPrimary,
        app->ble_selection.device.has_name ? app->ble_selection.device.name : "BLE Device");

    snprintf(
        app->ble_info_text,
        sizeof(app->ble_info_text),
        "%s\nCompany %s\nLooks %s\nProximity %s\nRSSI %d dBm\nAdvert %s\nMAC %s\nSaved %s\nSeen now %s",
        app->ble_selection.device.classification[0] ? app->ble_selection.device.classification : "BLE",
        app->ble_selection.device.has_company ? app->ble_selection.device.company : "unknown",
        app->ble_selection.device.has_appearance ? app->ble_selection.device.appearance : "unknown",
        app->ble_selection.device.proximity[0] ? app->ble_selection.device.proximity : "unknown",
        app->ble_selection.device.rssi,
        app->ble_selection.device.connectable ? "connectable" : "broadcast",
        app->ble_selection.device.mac,
        app->ble_selection.is_saved ? "yes" : "no",
        app->ble_selection.seen_recently ? "yes" : "no");

    widget_add_text_scroll_element(app->widget, 0, 16, 128, 34, app->ble_info_text);
}

void fuse_radio_app_refresh_gatt_services_submenu(FuseRadioApp* app) {
    furi_assert(app);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "GATT Services");

    if(app->gatt_results.svc_count == 0U) {
        return;
    }

    for(uint8_t i = 0; i < app->gatt_results.svc_count; i++) {
        const FuseRadioGattService* svc = &app->gatt_results.svcs[i];
        const char* label = (svc->name[0] && svc->name[0] != '-') ? svc->name : svc->uuid;
        submenu_add_item(
            app->submenu,
            label,
            i,
            fuse_radio_app_gatt_services_submenu_callback,
            app);
    }
}

void fuse_radio_app_refresh_gatt_chrs_widget(FuseRadioApp* app) {
    furi_assert(app);

    widget_reset(app->widget);

    if(app->ble_gatt_selected_svc >= app->gatt_results.svc_count) {
        widget_add_string_element(
            app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, "No service");
        return;
    }

    const FuseRadioGattService* svc = &app->gatt_results.svcs[app->ble_gatt_selected_svc];
    const char* header = (svc->name[0] && svc->name[0] != '-') ? svc->name : svc->uuid;
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, header);

    /* Build characteristics text for scroll element */
    static char chrs_text[512];
    size_t pos = 0;
    chrs_text[0] = '\0';

    for(uint8_t ci = 0; ci < svc->chr_count && pos < sizeof(chrs_text) - 1U; ci++) {
        const FuseRadioGattCharacteristic* chr = &svc->chrs[ci];
        const char* chr_label =
            (chr->name[0] && chr->name[0] != '-') ? chr->name : chr->uuid;
        int written;

        if(chr->has_value && chr->value[0] != '\0') {
            if(chr->has_raw && chr->raw[0] != '\0') {
                written = snprintf(
                    chrs_text + pos,
                    sizeof(chrs_text) - pos,
                    "%s\n  %s\n  %s\n  raw %s\n",
                    chr_label,
                    chr->props,
                    chr->value,
                    chr->raw);
            } else {
                written = snprintf(
                    chrs_text + pos,
                    sizeof(chrs_text) - pos,
                    "%s\n  %s\n  %s\n",
                    chr_label,
                    chr->props,
                    chr->value);
            }
        } else {
            written = snprintf(
                chrs_text + pos,
                sizeof(chrs_text) - pos,
                "%s\n  %s\n",
                chr_label,
                chr->props);
        }

        if(written > 0) {
            pos += (size_t)written;
        }
    }

    if(pos == 0) {
        snprintf(chrs_text, sizeof(chrs_text), "No characteristics");
    }

    widget_add_text_scroll_element(app->widget, 0, 15, 128, 38, chrs_text);
    app->gatt_dirty = false;
}

static void fuse_radio_app_append_promiscuous_text(FuseRadioApp* app, const char* fmt, ...) {
    const size_t used = strlen(app->promiscuous_info_text);
    if(used >= sizeof(app->promiscuous_info_text) - 1U) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(
        app->promiscuous_info_text + used, sizeof(app->promiscuous_info_text) - used, fmt, args);
    va_end(args);
    app->promiscuous_dirty = true;
}

static void fuse_radio_app_append_mdns_text(FuseRadioApp* app, const char* fmt, ...) {
    const size_t used = strlen(app->mdns_info_text);
    if(used >= sizeof(app->mdns_info_text) - 1U) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(app->mdns_info_text + used, sizeof(app->mdns_info_text) - used, fmt, args);
    va_end(args);
    app->mdns_dirty = true;
}

static void fuse_radio_app_append_http_text(FuseRadioApp* app, const char* fmt, ...) {
    const size_t used = strlen(app->http_info_text);
    if(used >= sizeof(app->http_info_text) - 1U) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(app->http_info_text + used, sizeof(app->http_info_text) - used, fmt, args);
    va_end(args);
    app->http_dirty = true;
}

static void fuse_radio_app_refresh_discover_progress_view(FuseRadioApp* app) {
    FuseRadioDiscoverProgressSnapshot snapshot = {
        .scanned_count = app->discover_results.scanned_count,
        .total_count = app->discover_results.total_hosts,
        .found_count = app->discover_results.found_count,
        .progress_percent = app->discover_results.progress_percent,
        .animation_frame = 0U,
    };

    fuse_radio_app_strlcpy(
        snapshot.subnet, app->discover_results.subnet, sizeof(snapshot.subnet));
    fuse_radio_app_strlcpy(
        snapshot.current_ip, app->discover_results.current_ip, sizeof(snapshot.current_ip));
    fuse_radio_discover_progress_view_set_snapshot(app->discover_progress_view, &snapshot);
    app->discover_dirty = false;
}

static void fuse_radio_app_refresh_discover_result_view(FuseRadioApp* app) {
    fuse_radio_discover_result_view_set_data(app->discover_result_view, &app->discover_results);
    app->discover_dirty = false;
}

static void fuse_radio_app_reset_wifi_status(FuseRadioApp* app) {
    app->wifi_mode = FuseRadioWifiModeIdle;
    app->wifi_action = FuseRadioWifiActionNone;
    app->wifi_connected = false;
    app->wifi_status_reason = 0U;
    app->connect_password_auto_used = false;
    strncpy(app->wifi_status_mode, "IDLE", sizeof(app->wifi_status_mode) - 1U);
    app->wifi_status_mode[sizeof(app->wifi_status_mode) - 1U] = '\0';
    strncpy(app->wifi_status_action, "NONE", sizeof(app->wifi_status_action) - 1U);
    app->wifi_status_action[sizeof(app->wifi_status_action) - 1U] = '\0';
    strncpy(app->wifi_status_state, "IDLE", sizeof(app->wifi_status_state) - 1U);
    app->wifi_status_state[sizeof(app->wifi_status_state) - 1U] = '\0';
    strncpy(app->wifi_status_ssid, "-", sizeof(app->wifi_status_ssid) - 1U);
    app->wifi_status_ssid[sizeof(app->wifi_status_ssid) - 1U] = '\0';
    fuse_radio_app_set_wifi_info_text(app, "Request a status update to poll the ESP.");
}

static void fuse_radio_app_update_wifi_info_text(FuseRadioApp* app) {
    snprintf(
        app->wifi_info_text,
        sizeof(app->wifi_info_text),
        "Mode: %s\nAction: %s\nState: %s\nConnected: %s\nSSID: %s\nReason: %u",
        app->wifi_status_mode,
        app->wifi_status_action,
        app->wifi_status_state,
        app->wifi_connected ? "yes" : "no",
        app->wifi_status_ssid,
        (unsigned)app->wifi_status_reason);
    app->wifi_info_dirty = true;
}

static void fuse_radio_app_set_wifi_mode(FuseRadioApp* app, FuseRadioWifiMode mode, const char* text) {
    app->wifi_mode = mode;
    fuse_radio_app_strlcpy(app->wifi_status_mode, text, sizeof(app->wifi_status_mode));
}

static void
    fuse_radio_app_set_wifi_action(FuseRadioApp* app, FuseRadioWifiAction action, const char* text) {
    app->wifi_action = action;
    fuse_radio_app_strlcpy(app->wifi_status_action, text, sizeof(app->wifi_status_action));
}

static void fuse_radio_app_set_wifi_mode_action(
    FuseRadioApp* app,
    FuseRadioWifiMode mode,
    const char* mode_text,
    FuseRadioWifiAction action,
    const char* action_text) {
    fuse_radio_app_set_wifi_mode(app, mode, mode_text);
    fuse_radio_app_set_wifi_action(app, action, action_text);
}

static bool fuse_radio_app_enable_otg(FuseRadioApp* app) {
    furi_assert(app);

    if(!app->power) {
        app->power = furi_record_open(RECORD_POWER);
    }

    PowerInfo power_info = {0};
    power_get_info(app->power, &power_info);
    if(power_info.voltage_vbus >= 4.5f) {
        app->otg_enabled = false;
        fuse_radio_app_set_status(app, "USB already provides 5V to VBUS.");
        return true;
    }

    power_enable_otg(app->power, true);
    power_get_info(app->power, &power_info);
    if(!power_info.is_otg_enabled) {
        return false;
    }

    app->otg_enabled = true;
    return true;
}

static void fuse_radio_app_reset_session_state(FuseRadioApp* app) {
    fuse_radio_app_reset_scan_results(app);
    fuse_radio_app_reset_ble_scan_results(app);
    fuse_radio_app_reset_discover_results(app);
    fuse_radio_app_reset_http_results(app);
    fuse_radio_app_reset_mdns_results(app);
    fuse_radio_app_reset_promiscuous_results(app);
    fuse_radio_app_reset_wifi_status(app);
    app->current_request = FuseRadioRequestNone;
    app->text_input_mode = FuseRadioTextInputNone;
    app->promiscuous_preset = FuseRadioPromiscuousPresetNone;
    app->http_preset = FuseRadioHttpPresetNone;
    app->connect_flow_active = false;
    app->line_length = 0U;
    app->line_overflow = false;
    app->module_detect_event_sent = false;
    app->last_error[0] = '\0';
}

static bool fuse_radio_app_begin_uart_detection(FuseRadioApp* app) {
    if(!app->insomnia_active) {
        furi_hal_power_insomnia_enter();
        app->insomnia_active = true;
    }

    app->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app->serial_handle) {
        fuse_radio_app_set_error(app, "Cannot acquire USART");
        return false;
    }

    furi_hal_serial_init(app->serial_handle, FUSE_RADIO_UART_BAUD_RATE);
    furi_hal_serial_configure_framing(
        app->serial_handle,
        FuriHalSerialDataBits8,
        FuriHalSerialParityNone,
        FuriHalSerialStopBits1);
    furi_hal_serial_async_rx_start(
        app->serial_handle, fuse_radio_app_serial_rx_callback, app, false);
    app->rx_started = true;

    app->detect_started_at = furi_get_tick();
    app->last_ping_at = 0U;
    app->last_mode_guard_poll_at = 0U;
    app->next_startup_action_at = app->detect_started_at + FUSE_RADIO_STARTUP_SETTLE_MS;
    app->module_state = FuseRadioModuleStateWaitingReady;
    fuse_radio_app_set_status(app, "Power stable. Waiting for ESP boot.");
    return true;
}

static bool fuse_radio_app_bootstrap_session(FuseRadioApp* app, bool reset_retry_state) {
    furi_assert(app);

    fuse_radio_app_reset_session_state(app);
    if(reset_retry_state) {
        app->startup_link_retry_count = 0U;
        app->startup_power_retry_count = 0U;
    }

    app->module_state = FuseRadioModuleStatePowering;
    fuse_radio_app_set_status(app, "Powering ESP module from OTG.");

    if(!app->expansion) {
        app->expansion = furi_record_open(RECORD_EXPANSION);
    }

    if(app->expansion && !app->expansion_disabled) {
        expansion_disable(app->expansion);
        app->expansion_disabled = true;
    }

    if(!app->otg_enabled) {
        if(!fuse_radio_app_enable_otg(app)) {
            app->startup_power_retry_count++;
            if(app->startup_power_retry_count >= FUSE_RADIO_STARTUP_MAX_POWER_RETRIES) {
                fuse_radio_app_set_error(app, "Cannot enable OTG 5V");
                return false;
            }

            app->module_state = FuseRadioModuleStatePowering;
            app->next_startup_action_at =
                furi_get_tick() + FUSE_RADIO_STARTUP_RETRY_DELAY_MS;
            fuse_radio_app_set_status(app, "Starting 5V rail. Retrying...");
            return true;
        }
    }

    app->startup_power_retry_count = 0U;

    if(app->otg_enabled) {
        fuse_radio_app_set_status(app, "OTG 5V enabled. Starting UART link.");
    } else {
        fuse_radio_app_set_status(app, "USB 5V detected. Starting UART link.");
    }

    return fuse_radio_app_begin_uart_detection(app);
}

static void fuse_radio_app_disable_otg(FuseRadioApp* app) {
    furi_assert(app);

    if(app->power && app->otg_enabled) {
        power_enable_otg(app->power, false);
        app->otg_enabled = false;
    }
}

void fuse_radio_app_set_status(FuseRadioApp* app, const char* detail) {
    furi_assert(app);
    furi_assert(detail);

    strncpy(app->status_detail, detail, sizeof(app->status_detail) - 1U);
    app->status_detail[sizeof(app->status_detail) - 1U] = '\0';
    app->status_dirty = true;
}

void fuse_radio_app_set_error(FuseRadioApp* app, const char* error) {
    furi_assert(app);
    furi_assert(error);

    app->module_state = FuseRadioModuleStateError;
    strncpy(app->last_error, error, sizeof(app->last_error) - 1U);
    app->last_error[sizeof(app->last_error) - 1U] = '\0';
    app->status_dirty = true;
}

static bool fuse_radio_app_send_line(FuseRadioApp* app, const char* line) {
    if(!app->serial_handle) {
        return false;
    }

    const size_t length = strlen(line);
    furi_hal_serial_tx(app->serial_handle, (const uint8_t*)line, length);
    furi_hal_serial_tx_wait_complete(app->serial_handle);
    return true;
}

bool fuse_radio_app_send_ping(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "PING\n");
}

void fuse_radio_app_set_ble_scan_mode(FuseRadioApp* app, FuseRadioBleScanMode mode) {
    furi_assert(app);
    app->ble_scan_mode = mode;
}

FuseRadioBleScanMode fuse_radio_app_get_ble_scan_mode(FuseRadioApp* app) {
    furi_assert(app);
    return app->ble_scan_mode;
}

static bool fuse_radio_app_send_wifi_scan(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "WIFI SCAN\n");
}

static bool fuse_radio_app_send_led_status(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "LED STATUS\n");
}

static bool fuse_radio_app_send_led_set(FuseRadioApp* app) {
    char line[48];

    snprintf(
        line,
        sizeof(line),
        "LED SET red=%u green=%u blue=%u\n",
        (unsigned)app->led_red,
        (unsigned)app->led_green,
        (unsigned)app->led_blue);
    return fuse_radio_app_send_line(app, line);
}

static bool fuse_radio_app_send_led_auto(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "LED AUTO\n");
}

static bool fuse_radio_app_send_ble_scan(FuseRadioApp* app) {
    char line[32];

    snprintf(line, sizeof(line), "BLE SCAN %u\n", (unsigned)fuse_radio_app_ble_scan_duration_ms(app));
    return fuse_radio_app_send_line(app, line);
}

static bool fuse_radio_app_send_ble_distance_start(FuseRadioApp* app) {
    char line[96];

    snprintf(
        line,
        sizeof(line),
        "BLE DISTANCE START mac=%s interval_ms=%u\n",
        app->ble_selection.device.mac,
        (unsigned)FUSE_RADIO_BLE_DISTANCE_INTERVAL_MS);
    return fuse_radio_app_send_line(app, line);
}

static bool fuse_radio_app_send_ble_distance_stop(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "BLE DISTANCE STOP\n");
}

static bool fuse_radio_app_send_ble_gatt(FuseRadioApp* app) {
    char line[64];

    snprintf(
        line,
        sizeof(line),
        "BLE GATT mac=%s addr_type=%s\n",
        app->ble_selection.device.mac,
        app->ble_selection.device.addr_type[0] ? app->ble_selection.device.addr_type : "RANDOM");
    return fuse_radio_app_send_line(app, line);
}

static void fuse_radio_app_reset_gatt_results(FuseRadioApp* app) {
    memset(&app->gatt_results, 0, sizeof(app->gatt_results));
}

static bool fuse_radio_app_format_quoted_arg(char* out, size_t out_size, const char* value) {
    size_t out_index = 0U;

    if(out_size < 3U) {
        return false;
    }

    out[out_index++] = '"';
    for(size_t index = 0; value[index] != '\0'; index++) {
        const char ch = value[index];
        if(ch == '"' || ch == '\\') {
            if(out_index + 2U >= out_size) {
                return false;
            }
            out[out_index++] = '\\';
        } else if(out_index + 1U >= out_size) {
            return false;
        }

        out[out_index++] = ch;
    }

    if(out_index + 2U > out_size) {
        return false;
    }

    out[out_index++] = '"';
    out[out_index] = '\0';
    return true;
}

static bool fuse_radio_app_send_wifi_connect_command(
    FuseRadioApp* app,
    const char* ssid,
    const char* password) {
    char quoted_ssid[96];
    char quoted_password[160];
    char command[320];

    if(!fuse_radio_app_format_quoted_arg(quoted_ssid, sizeof(quoted_ssid), ssid) ||
       !fuse_radio_app_format_quoted_arg(quoted_password, sizeof(quoted_password), password)) {
        return false;
    }

    snprintf(
        command, sizeof(command), "WIFI CONNECT ssid=%s psw=%s\n", quoted_ssid, quoted_password);
    return fuse_radio_app_send_line(app, command);
}

static bool fuse_radio_app_send_wifi_disconnect_command(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "WIFI DISCONNECT\n");
}

static bool fuse_radio_app_send_wifi_discover_command(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "WIFI DISCOVER\n");
}

static const char* fuse_radio_app_http_preset_command(FuseRadioHttpPreset preset) {
    switch(preset) {
    case FuseRadioHttpPresetPublicIp:
        return "ip";
    case FuseRadioHttpPresetTime:
        return "time";
    case FuseRadioHttpPresetLocation:
        return "location";
    case FuseRadioHttpPresetNone:
    default:
        return NULL;
    }
}

static const char* fuse_radio_app_http_preset_title(FuseRadioHttpPreset preset) {
    switch(preset) {
    case FuseRadioHttpPresetPublicIp:
        return "Public IP";
    case FuseRadioHttpPresetTime:
        return "Time";
    case FuseRadioHttpPresetLocation:
        return "Location";
    case FuseRadioHttpPresetNone:
    default:
        return "HTTP";
    }
}

static const char* fuse_radio_app_http_preset_start_text(FuseRadioHttpPreset preset) {
    switch(preset) {
    case FuseRadioHttpPresetPublicIp:
        return "Fetching public IP...\n";
    case FuseRadioHttpPresetTime:
        return "Fetching current time...\n";
    case FuseRadioHttpPresetLocation:
        return "Fetching approximate location...\n";
    case FuseRadioHttpPresetNone:
    default:
        return "Fetching HTTP preset...\n";
    }
}

static bool fuse_radio_app_send_wifi_http_command(FuseRadioApp* app, FuseRadioHttpPreset preset) {
    const char* command_name = fuse_radio_app_http_preset_command(preset);
    char command[64];

    if(!command_name) {
        return false;
    }

    snprintf(command, sizeof(command), "WIFI HTTP preset=%s\n", command_name);
    return fuse_radio_app_send_line(app, command);
}

static bool fuse_radio_app_send_wifi_mdns_command(FuseRadioApp* app, const char* host) {
    char command[128];
    snprintf(command, sizeof(command), "WIFI READ_MDNS host=%s\n", host);
    return fuse_radio_app_send_line(app, command);
}

static bool fuse_radio_app_send_wifi_promiscuous_enter_command(FuseRadioApp* app, uint8_t channel) {
    char command[64];
    snprintf(command, sizeof(command), "WIFI PROMISCUOUS ENTER channel=%u\n", channel);
    return fuse_radio_app_send_line(app, command);
}

static bool fuse_radio_app_send_wifi_promiscuous_exit_command(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "WIFI PROMISCUOUS EXIT\n");
}

static bool fuse_radio_app_send_wifi_promiscuous_survey_command(
    FuseRadioApp* app,
    FuseRadioSurveyPreset preset) {
    const FuseRadioSurveyPresetConfig* config = fuse_radio_app_get_survey_preset_config(preset);
    return fuse_radio_app_send_line(app, config->command);
}

static bool fuse_radio_app_send_wifi_promiscuous_watch_command(FuseRadioApp* app, uint8_t channel) {
    char command[64];
    snprintf(
        command,
        sizeof(command),
        "WIFI PROMISC WATCH channel=%u\n",
        channel);
    return fuse_radio_app_send_line(app, command);
}

static bool fuse_radio_app_send_wifi_promiscuous_watch_stop_command(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "WIFI PROMISC WATCH_STOP\n");
}

static bool fuse_radio_app_send_wifi_beacon_start_command(
    FuseRadioApp* app,
    uint8_t channel,
    uint32_t duration_ms) {
    char command[64];
    snprintf(
        command,
        sizeof(command),
        "WIFI BEACON START channel=%u duration_ms=%u\n",
        (unsigned)channel,
        (unsigned)duration_ms);
    return fuse_radio_app_send_line(app, command);
}

static bool fuse_radio_app_send_wifi_beacon_stop_command(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "WIFI BEACON STOP\n");
}

static bool fuse_radio_app_send_sd_info_command(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "SD INFO\n");
}

static bool fuse_radio_app_send_sd_list_command(FuseRadioApp* app, const char* path) {
    char quoted_path[192];
    char command[256];

    if(!fuse_radio_app_format_quoted_arg(quoted_path, sizeof(quoted_path), path)) {
        return false;
    }

    snprintf(
        command,
        sizeof(command),
        "SD LIST path=%s limit=%u\n",
        quoted_path,
        (unsigned)FUSE_RADIO_MAX_SD_ENTRIES);
    return fuse_radio_app_send_line(app, command);
}

static bool fuse_radio_app_send_sd_format_command(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "SD FORMAT\n");
}

bool fuse_radio_app_start_config_get(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_error(app, "Board is not ready.");
        return false;
    }
    app->current_request    = FuseRadioRequestConfigGet;
    app->config_entry_count = 0U;
    app->config_dirty       = false;
    app->config_sd_available = true; /* assume yes until told otherwise */
    return fuse_radio_app_send_line(app, "CONFIG GET\n");
}

bool fuse_radio_app_start_config_set(FuseRadioApp* app, const char* key, bool value) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        return false;
    }
    char command[80];
    snprintf(command, sizeof(command), "CONFIG SET key=%s value=%u\n", key, (unsigned)value);
    app->current_request = FuseRadioRequestConfigSet;
    return fuse_radio_app_send_line(app, command);
}

bool fuse_radio_app_start_wifi_beacon(FuseRadioApp* app, uint8_t channel, uint32_t duration_ms) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_error(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestBeaconStart;
    app->beacon_active = false;
    app->beacon_stop_pending = false;
    snprintf(
        app->beacon_info_text,
        sizeof(app->beacon_info_text),
        "Ch %u  %us\nStarting...",
        (unsigned)channel,
        (unsigned)(duration_ms / 1000U));
    app->promiscuous_dirty = true;

    if(!fuse_radio_app_send_wifi_beacon_start_command(app, channel, duration_ms)) {
        app->current_request = FuseRadioRequestNone;
        snprintf(
            app->beacon_info_text,
            sizeof(app->beacon_info_text),
            "UART write failed.");
        return false;
    }

    return true;
}

bool fuse_radio_app_stop_wifi_beacon(FuseRadioApp* app) {
    if(app->current_request != FuseRadioRequestBeaconStart || !app->beacon_active ||
       app->beacon_stop_pending) {
        return false;
    }

    app->beacon_stop_pending = true;
    snprintf(
        app->beacon_info_text,
        sizeof(app->beacon_info_text),
        "Stopping beacons...");
    app->promiscuous_dirty = true;

    if(!fuse_radio_app_send_wifi_beacon_stop_command(app)) {
        app->beacon_stop_pending = false;
        snprintf(
            app->beacon_info_text,
            sizeof(app->beacon_info_text),
            "Stop failed. Press OK to retry.");
        return false;
    }

    return true;
}

bool fuse_radio_app_request_wifi_status(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_wifi_info_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestStatus;
    fuse_radio_app_set_wifi_info_text(app, "Requesting WiFi status...");
    if(!fuse_radio_app_send_line(app, "WIFI STATUS\n")) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_set_wifi_info_text(app, "UART write failed while requesting status.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_connect(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_wifi_info_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestConnect;
    app->connect_flow_active = true;
    app->wifi_state = FuseRadioWifiStateConnectRequested;
    fuse_radio_app_set_wifi_mode_action(
        app,
        FuseRadioWifiModeIdle,
        "IDLE",
        FuseRadioWifiActionConnecting,
        "CONNECTING");
    snprintf(
        app->wifi_info_text,
        sizeof(app->wifi_info_text),
        "Entering connected mode via AP join.\nSSID: %s\nUse Refresh to poll status.",
        app->connect_ssid);
    app->wifi_info_dirty = true;

    if(!fuse_radio_app_send_wifi_connect_command(app, app->connect_ssid, app->connect_password)) {
        app->current_request = FuseRadioRequestNone;
        app->wifi_state = FuseRadioWifiStateError;
        fuse_radio_app_set_wifi_info_text(app, "UART write failed while starting connect.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_disconnect(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_wifi_info_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestDisconnect;
    app->connect_flow_active = false;
    app->wifi_state = FuseRadioWifiStateDisconnecting;
    fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionDisconnecting, "DISCONNECTING");
    fuse_radio_app_set_wifi_info_text(app, "Disconnecting from the active AP...");

    if(!fuse_radio_app_send_wifi_disconnect_command(app)) {
        app->current_request = FuseRadioRequestNone;
        app->wifi_state = FuseRadioWifiStateError;
        fuse_radio_app_set_wifi_info_text(app, "UART write failed while disconnecting.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_discover(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_reset_discover_results(app);
        app->discover_results.has_error = true;
        fuse_radio_app_strlcpy(
            app->discover_results.error,
            "Board is not ready.",
            sizeof(app->discover_results.error));
        return false;
    }

    app->current_request = FuseRadioRequestDiscover;
    fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionDiscovering, "DISCOVERING");
    fuse_radio_app_reset_discover_results(app);
    app->discover_results.active = true;

    if(!fuse_radio_app_send_wifi_discover_command(app)) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_reset_discover_results(app);
        app->discover_results.has_error = true;
        fuse_radio_app_strlcpy(
            app->discover_results.error,
            "UART write failed while starting discovery.",
            sizeof(app->discover_results.error));
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_http_request(FuseRadioApp* app, FuseRadioHttpPreset preset) {
    app->http_preset = preset;
    fuse_radio_app_strlcpy(
        app->http_title, fuse_radio_app_http_preset_title(preset), sizeof(app->http_title));

    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_reset_http_results(app);
        fuse_radio_app_append_http_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestHttp;
    fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionRequestingHttp, "REQUESTING_HTTP");
    fuse_radio_app_reset_http_results(app);
    fuse_radio_app_append_http_text(app, "%s", fuse_radio_app_http_preset_start_text(preset));

    if(!fuse_radio_app_send_wifi_http_command(app, preset)) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        fuse_radio_app_reset_http_results(app);
        fuse_radio_app_append_http_text(app, "UART write failed while starting HTTP request.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_mdns_query(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_reset_mdns_results(app);
        fuse_radio_app_append_mdns_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestMdns;
    fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionResolvingMdns, "RESOLVING_MDNS");
    fuse_radio_app_reset_mdns_results(app);
    fuse_radio_app_append_mdns_text(app, "Querying %s...\n", app->mdns_host);

    if(!fuse_radio_app_send_wifi_mdns_command(app, app->mdns_host)) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_reset_mdns_results(app);
        fuse_radio_app_append_mdns_text(app, "UART write failed while querying mDNS.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_promiscuous_enter(FuseRadioApp* app, uint8_t channel) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_reset_promiscuous_results(app);
        fuse_radio_app_append_promiscuous_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestPromiscuousEnter;
    app->promiscuous_preset = FuseRadioPromiscuousPresetEnterChannel1;
    fuse_radio_app_set_wifi_mode_action(
        app,
        FuseRadioWifiModeIdle,
        "IDLE",
        FuseRadioWifiActionEnteringPromiscuous,
        "ENTERING_PROMISCUOUS");
    fuse_radio_app_reset_promiscuous_results(app);
    fuse_radio_app_append_promiscuous_text(app, "Opening promiscuous tools...\nDefault channel: %u\n\n", channel);

    if(!fuse_radio_app_send_wifi_promiscuous_enter_command(app, channel)) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        fuse_radio_app_reset_promiscuous_results(app);
        fuse_radio_app_append_promiscuous_text(app, "UART write failed while entering promiscuous mode.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_promiscuous_exit(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_reset_promiscuous_results(app);
        fuse_radio_app_append_promiscuous_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestPromiscuousExit;
    app->promiscuous_preset = FuseRadioPromiscuousPresetExit;
    fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionExitingPromiscuous, "EXITING_PROMISCUOUS");
    fuse_radio_app_reset_promiscuous_results(app);
    fuse_radio_app_append_promiscuous_text(app, "Leaving promiscuous mode...\n");

    if(!fuse_radio_app_send_wifi_promiscuous_exit_command(app)) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        fuse_radio_app_reset_promiscuous_results(app);
        fuse_radio_app_append_promiscuous_text(app, "UART write failed while exiting promiscuous mode.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_promiscuous_survey(FuseRadioApp* app, FuseRadioSurveyPreset preset) {
    const FuseRadioSurveyPresetConfig* config = fuse_radio_app_get_survey_preset_config(preset);

    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_reset_promiscuous_results(app);
        fuse_radio_app_append_promiscuous_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestPromiscuousSurvey;
    app->promiscuous_preset = config->repeat_preset;
    fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionSurveying, "SURVEYING");
    fuse_radio_app_reset_promiscuous_results(app);
    fuse_radio_app_prepare_survey_state(app, preset);
    fuse_radio_app_append_promiscuous_text(app, "Survey running\n");

    if(!fuse_radio_app_send_wifi_promiscuous_survey_command(app, preset)) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        fuse_radio_app_reset_promiscuous_results(app);
        fuse_radio_app_append_promiscuous_text(app, "UART write failed while starting survey.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_promiscuous_watch(FuseRadioApp* app, uint8_t channel) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_reset_promiscuous_results(app);
        fuse_radio_app_append_promiscuous_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestPromiscuousWatch;
    app->promiscuous_preset = FuseRadioPromiscuousPresetWatchChannel;
    app->promiscuous_watch_channel = channel;

    fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionWatching, "WATCHING");
    fuse_radio_app_reset_promiscuous_results(app);
    app->promiscuous_watch_live_active = true;
    app->promiscuous_live_max_rssi = -127;
    fuse_radio_app_set_promiscuous_live_text(
        app,
        "Ch %u\nStarting watch...\n\nFrames: -\nUnique: -\nBeacons: -\nRSSI: -",
        channel);

    if(!fuse_radio_app_send_wifi_promiscuous_watch_command(app, channel)) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        fuse_radio_app_reset_promiscuous_results(app);
        fuse_radio_app_append_promiscuous_text(app, "UART write failed while starting watch mode.");
        return false;
    }

    return true;
}

bool fuse_radio_app_stop_wifi_promiscuous_watch(FuseRadioApp* app) {
    if(app->current_request != FuseRadioRequestPromiscuousWatch ||
       !app->promiscuous_watch_live_active || app->promiscuous_watch_stop_pending) {
        return false;
    }

    app->promiscuous_watch_stop_pending = true;
    fuse_radio_app_set_promiscuous_live_text(
        app,
        "Ch %u\nStopping watch...\n\nFrames: pending\nUnique: pending\nBeacons: pending\nRSSI: pending",
        app->promiscuous_watch_channel);

    if(!fuse_radio_app_send_wifi_promiscuous_watch_stop_command(app)) {
        app->promiscuous_watch_stop_pending = false;
        fuse_radio_app_set_promiscuous_live_text(
            app,
            "Ch %u\nStop failed\n\nPress center to retry stop.",
            app->promiscuous_watch_channel);
        return false;
    }

    return true;
}

bool fuse_radio_app_repeat_wifi_promiscuous_action(FuseRadioApp* app) {
    switch(app->promiscuous_preset) {
    case FuseRadioPromiscuousPresetEnterChannel1:
        return fuse_radio_app_start_wifi_promiscuous_enter(app, 1U);
    case FuseRadioPromiscuousPresetExit:
        return fuse_radio_app_start_wifi_promiscuous_exit(app);
    case FuseRadioPromiscuousPresetSurvey11611:
        return fuse_radio_app_start_wifi_promiscuous_survey(app, FuseRadioSurveyPreset11611);
    case FuseRadioPromiscuousPresetSurvey111:
        return fuse_radio_app_start_wifi_promiscuous_survey(app, FuseRadioSurveyPreset111);
    case FuseRadioPromiscuousPresetSurvey14814:
        return fuse_radio_app_start_wifi_promiscuous_survey(app, FuseRadioSurveyPreset14814);
    case FuseRadioPromiscuousPresetSurvey1357911:
        return fuse_radio_app_start_wifi_promiscuous_survey(app, FuseRadioSurveyPreset1357911);
    case FuseRadioPromiscuousPresetWatchChannel:
        return fuse_radio_app_start_wifi_promiscuous_watch(app, app->promiscuous_watch_channel);
    case FuseRadioPromiscuousPresetNone:
    default:
        return false;
    }
}

static void fuse_radio_app_mark_detected(FuseRadioApp* app) {
    app->module_state = FuseRadioModuleStateDetected;
    app->module_detect_event_sent = false;
    app->startup_link_retry_count = 0U;
    app->startup_power_retry_count = 0U;
    app->last_error[0] = '\0';
    fuse_radio_app_set_status(app, "Board detected. Opening control menu.");
}

static void fuse_radio_app_parse_mdns_line(FuseRadioApp* app, const char* line) {
    const char* host = strstr(line, "host=");
    const char* addr = strstr(line, " addr=");
    const char* family = strstr(line, " family=");

    if(!host || !addr || !family) {
        fuse_radio_app_append_mdns_text(app, "%s\n", line);
        return;
    }

    host += 5;
    addr += 6;
    family += 8;

    char host_text[64] = {0};
    char addr_text[64] = {0};
    char family_text[16] = {0};
    size_t host_len = (size_t)(strstr(host, " addr=") - host);
    size_t addr_len = (size_t)(strstr(addr, " family=") - addr);
    size_t family_len = strcspn(family, " ");

    if(host_len >= sizeof(host_text)) host_len = sizeof(host_text) - 1U;
    if(addr_len >= sizeof(addr_text)) addr_len = sizeof(addr_text) - 1U;
    if(family_len >= sizeof(family_text)) family_len = sizeof(family_text) - 1U;

    memcpy(host_text, host, host_len);
    memcpy(addr_text, addr, addr_len);
    memcpy(family_text, family, family_len);

    fuse_radio_app_append_mdns_text(app, "%s\n%s (%s)\n\n", host_text, addr_text, family_text);
}

static void fuse_radio_app_parse_http_line(FuseRadioApp* app, const char* line) {
    if(strncmp(line, "HTTP_LINE ", 10) == 0) {
        fuse_radio_app_append_http_text(app, "%s\n", line + 10);
    } else {
        fuse_radio_app_append_http_text(app, "%s\n", line);
    }
}

static void fuse_radio_app_format_bytes(char* out, size_t out_size, uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    uint64_t whole = bytes;
    uint8_t tenth = 0U;
    size_t unit = 0U;

    while(whole >= 1024U && unit < (sizeof(units) / sizeof(units[0])) - 1U) {
        tenth = (uint8_t)(((whole % 1024U) * 10U) / 1024U);
        whole /= 1024U;
        unit++;
    }

    if(unit == 0U) {
        snprintf(out, out_size, "%llu %s", (unsigned long long)bytes, units[unit]);
    } else if(whole >= 10U || tenth == 0U) {
        snprintf(out, out_size, "%llu %s", (unsigned long long)whole, units[unit]);
    } else {
        snprintf(out, out_size, "%llu.%u %s", (unsigned long long)whole, (unsigned)tenth, units[unit]);
    }
}

/* ---------------------------------------------------------------------------
 * CONFIG line parsing helpers
 * --------------------------------------------------------------------------- */

/* Copy an unquoted or quoted value for a named token, e.g. key=foo or label="Foo Bar" */
static bool fuse_radio_app_config_extract(
    const char* line,
    const char* key_eq,  /* e.g. "key=" */
    char*       out,
    size_t      out_size) {
    const char* pos = strstr(line, key_eq);
    if(pos == NULL) {
        return false;
    }
    pos += strlen(key_eq);

    size_t i = 0U;
    if(*pos == '"') {
        pos++;
        while(*pos != '\0' && *pos != '"' && i + 1U < out_size) {
            out[i++] = *pos++;
        }
    } else {
        while(*pos != '\0' && *pos != ' ' && i + 1U < out_size) {
            out[i++] = *pos++;
        }
    }
    out[i] = '\0';
    return i > 0U;
}

static void fuse_radio_app_parse_config_entry(FuseRadioApp* app, const char* line) {
    if(app->config_entry_count >= FUSE_RADIO_MAX_CONFIG_ENTRIES) {
        return;
    }

    char key[FUSE_RADIO_CONFIG_KEY_SIZE];
    char label[FUSE_RADIO_CONFIG_LABEL_SIZE];
    char type_str[8];
    char value_str[8];

    if(!fuse_radio_app_config_extract(line, "key=", key, sizeof(key))) {
        return;
    }
    if(!fuse_radio_app_config_extract(line, "type=", type_str, sizeof(type_str))) {
        return;
    }
    if(!fuse_radio_app_config_extract(line, "value=", value_str, sizeof(value_str))) {
        return;
    }
    if(!fuse_radio_app_config_extract(line, "label=", label, sizeof(label))) {
        fuse_radio_app_strlcpy(label, key, sizeof(label));
    }

    uint8_t idx = app->config_entry_count;

    fuse_radio_app_strlcpy(
        app->config_entries[idx].key, key, sizeof(app->config_entries[idx].key));
    fuse_radio_app_strlcpy(
        app->config_entries[idx].label, label, sizeof(app->config_entries[idx].label));

    if(strcmp(type_str, "bool") == 0) {
        app->config_entries[idx].type       = 0U;
        app->config_entries[idx].bool_value = (value_str[0] == '1');
    } else {
        app->config_entries[idx].type      = 1U;
        app->config_entries[idx].int_value = (int32_t)strtol(value_str, NULL, 10);
    }

    app->config_entry_count++;
}

static void fuse_radio_app_parse_sd_info(FuseRadioApp* app, const char* line) {
    const char* present = strstr(line, "present=");
    const char* mounted = strstr(line, " mounted=");
    const char* total = strstr(line, " total_bytes=");
    const char* free = strstr(line, " free_bytes=");
    uint64_t total_bytes = 0U;
    uint64_t free_bytes = 0U;
    uint64_t used_bytes = 0U;
    char total_h[24];
    char free_h[24];
    char used_h[24];
    unsigned used_percent = 0U;

    if(!present || !mounted || !total || !free) {
        return;
    }

    total_bytes = strtoull(total + 13, NULL, 10);
    free_bytes = strtoull(free + 12, NULL, 10);
    used_bytes = (total_bytes >= free_bytes) ? (total_bytes - free_bytes) : 0U;

    if(total_bytes > 0U) {
        used_percent = (unsigned)((used_bytes * 100ULL) / total_bytes);
    }

    fuse_radio_app_format_bytes(total_h, sizeof(total_h), total_bytes);
    fuse_radio_app_format_bytes(free_h, sizeof(free_h), free_bytes);
    fuse_radio_app_format_bytes(used_h, sizeof(used_h), used_bytes);

    snprintf(
        app->sd_info_text,
        sizeof(app->sd_info_text),
        "Card: %s\n"
        "Mounted: %s\n"
        "Path: %s\n\n"
        "Total: %s\n"
        "Free: %s\n"
        "Used: %s (%u%%)\n\n"
        "Raw total: %llu\n"
        "Raw free: %llu",
        strncmp(present + 8, "yes", 3) == 0 ? "Present" : "Missing",
        strncmp(mounted + 9, "yes", 3) == 0 ? "Yes" : "No",
        app->sd_explore_path[0] ? app->sd_explore_path : "/",
        total_h,
        free_h,
        used_h,
        used_percent,
        (unsigned long long)total_bytes,
        (unsigned long long)free_bytes);
    app->sd_dirty = true;
}

static void fuse_radio_app_parse_sd_entry(FuseRadioApp* app, const char* line) {
    const char* type = strstr(line, "type=");
    const char* size = strstr(line, " size=");
    const char* name = strstr(line, " name=");
    char type_text[8] = {0};
    size_t type_len;

    if(!type || !size || !name || app->sd_entry_count >= FUSE_RADIO_MAX_SD_ENTRIES) {
        return;
    }

    type += 5;
    type_len = (size_t)(size - type);
    if(type_len >= sizeof(type_text)) {
        type_len = sizeof(type_text) - 1U;
    }
    memcpy(type_text, type, type_len);

    FuseRadioSdEntry* entry = &app->sd_entries[app->sd_entry_count++];
    entry->is_dir = strcmp(type_text, "dir") == 0;
    fuse_radio_app_strlcpy(entry->name, name + 6, sizeof(entry->name));
    app->sd_dirty = true;
}

static void fuse_radio_app_refresh_sd_explore_submenu(FuseRadioApp* app) {
    furi_assert(app);

    submenu_reset(app->submenu);

    if(strcmp(app->sd_explore_path, "/") != 0) {
        submenu_add_item(
            app->submenu,
            "..",
            1000U,
            fuse_radio_app_sd_explore_submenu_callback,
            app);
    }

    if(app->sd_entry_count == 0U) {
        submenu_add_item(
            app->submenu,
            "(empty)",
            1001U,
            fuse_radio_app_sd_explore_submenu_callback,
            app);
    } else {
        for(uint8_t index = 0U; index < app->sd_entry_count; index++) {
            submenu_add_item(
                app->submenu,
                app->sd_entries[index].name,
                index,
                fuse_radio_app_sd_explore_submenu_callback,
                app);
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
    app->sd_dirty = false;
}

static void fuse_radio_app_parse_discover_network_line(FuseRadioApp* app, const char* line) {
    const char* subnet = strstr(line, "subnet=");
    const char* self = strstr(line, " self=");
    const char* gateway = strstr(line, " gw=");
    const char* hosts = strstr(line, " hosts=");

    if(!subnet || !self) {
        return;
    }

    subnet += 7;
    self += 6;

    char subnet_text[FUSE_RADIO_DISCOVER_SUBNET_SIZE] = {0};
    char self_text[sizeof(app->discover_results.self_ip)] = {0};
    char gateway_text[sizeof(app->discover_results.gateway_ip)] = {0};
    size_t subnet_len = (size_t)(strstr(subnet, " self=") - subnet);
    size_t self_len = gateway ? (size_t)(gateway - self) : (hosts ? (size_t)(hosts - self) : strcspn(self, " "));
    size_t gateway_len = 0U;

    if(subnet_len >= sizeof(subnet_text)) subnet_len = sizeof(subnet_text) - 1U;
    if(self_len >= sizeof(self_text)) self_len = sizeof(self_text) - 1U;
    if(gateway && hosts) {
        gateway_len = (size_t)(hosts - (gateway + 4));
    } else if(gateway) {
        gateway_len = strcspn(gateway + 4, " ");
    }
    if(gateway_len >= sizeof(gateway_text)) gateway_len = sizeof(gateway_text) - 1U;

    memcpy(subnet_text, subnet, subnet_len);
    memcpy(self_text, self, self_len);
    if(gateway_len > 0U) {
        memcpy(gateway_text, gateway + 4, gateway_len);
    }

    fuse_radio_app_strlcpy(
        app->discover_results.subnet, subnet_text, sizeof(app->discover_results.subnet));
    fuse_radio_app_strlcpy(
        app->discover_results.self_ip, self_text, sizeof(app->discover_results.self_ip));
    fuse_radio_app_strlcpy(
        app->discover_results.gateway_ip,
        gateway_text,
        sizeof(app->discover_results.gateway_ip));
    if(hosts) {
        app->discover_results.total_hosts = (uint16_t)strtoul(hosts + 7, NULL, 10);
    }
    app->discover_dirty = true;
}

static void fuse_radio_app_parse_discover_progress_line(FuseRadioApp* app, const char* line) {
    const char* scanned = strstr(line, "scanned=");
    const char* total = strstr(line, " total=");
    const char* found = strstr(line, " found=");
    const char* current = strstr(line, " current=");

    if(scanned) {
        app->discover_results.scanned_count = (uint16_t)strtoul(scanned + 8, NULL, 10);
    }
    if(total) {
        app->discover_results.total_hosts = (uint16_t)strtoul(total + 7, NULL, 10);
    }
    if(found) {
        app->discover_results.found_count = (uint16_t)strtoul(found + 7, NULL, 10);
    }
    if(current) {
        const size_t current_len = strcspn(current + 9, " ");
        char current_text[sizeof(app->discover_results.current_ip)] = {0};
        size_t copy_len = current_len;

        if(copy_len >= sizeof(current_text)) {
            copy_len = sizeof(current_text) - 1U;
        }

        memcpy(current_text, current + 9, copy_len);
        fuse_radio_app_strlcpy(
            app->discover_results.current_ip,
            current_text,
            sizeof(app->discover_results.current_ip));
    }

    if(app->discover_results.total_hosts > 0U) {
        app->discover_results.progress_percent = (uint8_t)(
            (app->discover_results.scanned_count * 100U) / app->discover_results.total_hosts);
        if(app->discover_results.progress_percent > 100U) {
            app->discover_results.progress_percent = 100U;
        }
    }

    app->discover_dirty = true;
}

static void fuse_radio_app_parse_discover_found_line(FuseRadioApp* app, const char* line) {
    const char* ip = strstr(line, "ip=");
    const char* host = strstr(line, " host=");
    const char* source = strstr(line, " source=");
    const char* mac = strstr(line, " mac=");
    const char* vendor = strstr(line, " vendor=");
    const char* role = strstr(line, " role=");
    const char* services = strstr(line, " services=");
    const char* rtt = strstr(line, " rtt_ms=");

    if(!ip || !host || !source || !mac || !vendor || !role || !services || !rtt) {
        return;
    }

    ip += 3;
    host += 6;
    source += 8;
    mac += 5;
    vendor += 8;
    role += 6;
    services += 10;
    rtt += 8;

    char ip_text[16] = {0};
    char host_text[FUSE_RADIO_DISCOVER_HOSTNAME_SIZE] = {0};
    char source_text[16] = {0};
    char mac_text[sizeof(((FuseRadioDiscoverHost*)0)->mac)] = {0};
    char vendor_text[FUSE_RADIO_DISCOVER_VENDOR_SIZE] = {0};
    char role_text[FUSE_RADIO_DISCOVER_ROLE_SIZE] = {0};
    char services_text[FUSE_RADIO_DISCOVER_SERVICES_SIZE] = {0};
    char rtt_text[16] = {0};
    size_t ip_len = (size_t)(strstr(ip, " host=") - ip);
    size_t host_len = (size_t)(strstr(host, " source=") - host);
    size_t source_len = (size_t)(strstr(source, " mac=") - source);
    size_t mac_len = (size_t)(strstr(mac, " vendor=") - mac);
    size_t vendor_len = (size_t)(strstr(vendor, " role=") - vendor);
    size_t role_len = (size_t)(strstr(role, " services=") - role);
    size_t services_len = (size_t)(strstr(services, " rtt_ms=") - services);
    size_t rtt_len = strcspn(rtt, " ");

    if(ip_len >= sizeof(ip_text)) ip_len = sizeof(ip_text) - 1U;
    if(host_len >= sizeof(host_text)) host_len = sizeof(host_text) - 1U;
    if(source_len >= sizeof(source_text)) source_len = sizeof(source_text) - 1U;
    if(mac_len >= sizeof(mac_text)) mac_len = sizeof(mac_text) - 1U;
    if(vendor_len >= sizeof(vendor_text)) vendor_len = sizeof(vendor_text) - 1U;
    if(role_len >= sizeof(role_text)) role_len = sizeof(role_text) - 1U;
    if(services_len >= sizeof(services_text)) services_len = sizeof(services_text) - 1U;
    if(rtt_len >= sizeof(rtt_text)) rtt_len = sizeof(rtt_text) - 1U;

    memcpy(ip_text, ip, ip_len);
    memcpy(host_text, host, host_len);
    memcpy(source_text, source, source_len);
    memcpy(mac_text, mac, mac_len);
    memcpy(vendor_text, vendor, vendor_len);
    memcpy(role_text, role, role_len);
    memcpy(services_text, services, services_len);
    memcpy(rtt_text, rtt, rtt_len);

    if(app->discover_results.count < FUSE_RADIO_MAX_DISCOVER_HOSTS) {
        FuseRadioDiscoverHost* discover_host =
            &app->discover_results.hosts[app->discover_results.count++];

        fuse_radio_app_strlcpy(discover_host->ip, ip_text, sizeof(discover_host->ip));
        discover_host->rtt_ms = (uint16_t)strtoul(rtt_text, NULL, 10);
        discover_host->has_name = strcmp(host_text, "-") != 0;
        if(discover_host->has_name) {
            fuse_radio_app_strlcpy(discover_host->name, host_text, sizeof(discover_host->name));
        }
        discover_host->has_mac = strcmp(mac_text, "-") != 0;
        if(discover_host->has_mac) {
            fuse_radio_app_strlcpy(discover_host->mac, mac_text, sizeof(discover_host->mac));
        }
        discover_host->has_vendor = strcmp(vendor_text, "-") != 0;
        if(discover_host->has_vendor) {
            fuse_radio_app_strlcpy(
                discover_host->vendor, vendor_text, sizeof(discover_host->vendor));
        }
        discover_host->has_role = strcmp(role_text, "-") != 0;
        if(discover_host->has_role) {
            fuse_radio_app_strlcpy(discover_host->role, role_text, sizeof(discover_host->role));
        }
        discover_host->has_services = strcmp(services_text, "-") != 0;
        if(discover_host->has_services) {
            fuse_radio_app_strlcpy(
                discover_host->services,
                services_text,
                sizeof(discover_host->services));
        }
        discover_host->is_gateway =
            app->discover_results.gateway_ip[0] != '\0' &&
            strcmp(discover_host->ip, app->discover_results.gateway_ip) == 0;

        if(strcmp(source_text, "mdns") == 0) {
            discover_host->name_source = FuseRadioDiscoverNameSourceMdns;
        } else if(strcmp(source_text, "rdns") == 0) {
            discover_host->name_source = FuseRadioDiscoverNameSourceReverseDns;
        } else {
            discover_host->name_source = FuseRadioDiscoverNameSourceNone;
        }
    } else {
        app->discover_results.truncated_count++;
    }

    app->discover_dirty = true;
}

static bool fuse_radio_app_parse_ap_line(FuseRadioApp* app, const char* line) {
    if(app->scan_results.count >= FUSE_RADIO_MAX_AP_COUNT) {
        return false;
    }

    const char* ssid_begin = line + 3;
    const char* rssi_tag = strstr(ssid_begin, " RSSI ");
    const char* auth_tag = rssi_tag ? strstr(rssi_tag + 6, " AUTH ") : NULL;
    const char* ch_tag = auth_tag ? strstr(auth_tag + 6, " CH ") : NULL;

    if(!rssi_tag || !auth_tag || !ch_tag) {
        return false;
    }

    FuseRadioAccessPoint* ap = &app->scan_results.aps[app->scan_results.count];
    const size_t ssid_size = (size_t)(rssi_tag - ssid_begin);
    const size_t auth_size = (size_t)(ch_tag - (auth_tag + 6));
    int rssi = 0;
    unsigned long channel = 0;

    if(ssid_size > FUSE_RADIO_MAX_SSID_LENGTH || auth_size > FUSE_RADIO_MAX_AUTH_LENGTH) {
        return false;
    }

    memcpy(ap->ssid, ssid_begin, ssid_size);
    ap->ssid[ssid_size] = '\0';
    memcpy(ap->auth, auth_tag + 6, auth_size);
    ap->auth[auth_size] = '\0';

    rssi = (int)strtol(rssi_tag + 6, NULL, 10);
    channel = strtoul(ch_tag + 4, NULL, 10);

    ap->rssi = (int16_t)rssi;
    ap->channel = (uint8_t)channel;

    app->scan_results.count++;
    app->scan_dirty = true;
    return true;
}

static bool fuse_radio_app_parse_ble_device_line(FuseRadioApp* app, const char* line) {
    if(app->ble_scan_results.count >= FUSE_RADIO_MAX_BLE_DEVICES) {
        return false;
    }

    const char* mac_begin = line + 11;
    const char* rssi_tag = strstr(mac_begin, " RSSI ");
    const char* company_tag = rssi_tag ? strstr(rssi_tag + 6, " COMPANY ") : NULL;
    const char* appear_tag = company_tag ? strstr(company_tag + 9, " APPEAR ") : NULL;
    const char* class_tag = appear_tag ? strstr(appear_tag + 8, " CLASS ") : NULL;
    const char* prox_tag = class_tag ? strstr(class_tag + 7, " PROX ") : NULL;
    const char* addr_tag = prox_tag ? strstr(prox_tag + 6, " ADDR ") : NULL;
    const char* conn_tag = addr_tag ? strstr(addr_tag + 6, " CONN ") : NULL;
    const char* name_tag = conn_tag ? strstr(conn_tag + 6, " NAME ") : NULL;

    if(!rssi_tag || !company_tag || !appear_tag || !class_tag || !prox_tag || !addr_tag ||
       !conn_tag || !name_tag) {
        return false;
    }

    FuseRadioBleDevice* device = &app->ble_scan_results.devices[app->ble_scan_results.count];
    const size_t mac_size = (size_t)(rssi_tag - mac_begin);
    const size_t company_size = (size_t)(appear_tag - (company_tag + 9));
    const size_t appearance_size = (size_t)(class_tag - (appear_tag + 8));
    const size_t class_size = (size_t)(prox_tag - (class_tag + 7));
    const size_t proximity_size = (size_t)(addr_tag - (prox_tag + 6));
    const size_t addr_size = (size_t)(conn_tag - (addr_tag + 6));
    const size_t conn_size = (size_t)(name_tag - (conn_tag + 6));
    const char* name_begin = name_tag + 6;
    const size_t name_size = strlen(name_begin);
    char company_text[FUSE_RADIO_BLE_COMPANY_SIZE] = {0};
    char appearance_text[FUSE_RADIO_BLE_APPEARANCE_SIZE] = {0};
    char class_text[FUSE_RADIO_BLE_CLASS_SIZE] = {0};
    char proximity_text[FUSE_RADIO_BLE_PROXIMITY_SIZE] = {0};
    char conn_text[8] = {0};

    if(mac_size >= sizeof(device->mac) || company_size >= sizeof(company_text) ||
       appearance_size >= sizeof(appearance_text) || class_size >= sizeof(class_text) ||
       proximity_size >= sizeof(proximity_text) || addr_size >= sizeof(device->addr_type) ||
       conn_size >= sizeof(conn_text)) {
        return false;
    }

    memcpy(device->mac, mac_begin, mac_size);
    device->mac[mac_size] = '\0';
    memcpy(company_text, company_tag + 9, company_size);
    company_text[company_size] = '\0';
    memcpy(appearance_text, appear_tag + 8, appearance_size);
    appearance_text[appearance_size] = '\0';
    memcpy(class_text, class_tag + 7, class_size);
    class_text[class_size] = '\0';
    memcpy(proximity_text, prox_tag + 6, proximity_size);
    proximity_text[proximity_size] = '\0';
    memcpy(device->addr_type, addr_tag + 6, addr_size);
    device->addr_type[addr_size] = '\0';
    memcpy(conn_text, conn_tag + 6, conn_size);
    conn_text[conn_size] = '\0';

    device->rssi = (int16_t)strtol(rssi_tag + 6, NULL, 10);
    device->connectable = strcmp(conn_text, "YES") == 0;
    device->has_company = strcmp(company_text, "-") != 0;
    if(device->has_company) {
        fuse_radio_app_strlcpy(device->company, company_text, sizeof(device->company));
    } else {
        device->company[0] = '\0';
    }
    device->has_appearance = strcmp(appearance_text, "-") != 0;
    if(device->has_appearance) {
        fuse_radio_app_strlcpy(device->appearance, appearance_text, sizeof(device->appearance));
    } else {
        device->appearance[0] = '\0';
    }
    fuse_radio_app_strlcpy(device->classification, class_text, sizeof(device->classification));
    fuse_radio_app_strlcpy(device->proximity, proximity_text, sizeof(device->proximity));
    device->has_name = strcmp(name_begin, "-") != 0;
    if(device->has_name) {
        const size_t copy_size =
            name_size >= sizeof(device->name) ? sizeof(device->name) - 1U : name_size;
        memcpy(device->name, name_begin, copy_size);
        device->name[copy_size] = '\0';
    } else {
        device->name[0] = '\0';
    }

    app->ble_scan_results.count++;
    fuse_radio_app_touch_saved_ble_device(app, device);
    app->ble_dirty = true;
    return true;
}

static bool fuse_radio_app_parse_ble_distance_sample_line(FuseRadioApp* app, const char* line) {
    const char* rssi = strstr(line, "rssi=");
    const char* seen = strstr(line, " seen=");
    const char* trend = strstr(line, " trend=");
    const char* dist = strstr(line, " dist_dm=");
    const char* samples = strstr(line, " samples=");

    if(!rssi || !seen || !trend || !dist || !samples) {
        return false;
    }

    app->ble_distance_rssi = (int16_t)strtol(rssi + 5, NULL, 10);
    app->ble_distance_seen = strtoul(seen + 6, NULL, 10) > 0UL;
    app->ble_distance_distance_dm = (int16_t)strtol(dist + 9, NULL, 10);
    app->ble_distance_samples = (uint16_t)strtoul(samples + 9, NULL, 10);

    const char* trend_begin = trend + 7;
    size_t trend_len = (size_t)(dist - trend_begin);
    if(trend_len >= sizeof(app->ble_distance_trend)) {
        trend_len = sizeof(app->ble_distance_trend) - 1U;
    }
    memcpy(app->ble_distance_trend, trend_begin, trend_len);
    app->ble_distance_trend[trend_len] = '\0';

    if(!app->ble_distance_seen) {
        app->ble_distance_missed_scans++;
    }

    app->ble_distance_has_error = false;
    app->ble_dirty = true;
    return true;
}

static void fuse_radio_app_parse_wifi_status(FuseRadioApp* app, const char* line) {
    const FuseRadioWifiState previous_wifi_state = app->wifi_state;
    const char* mode = strstr(line, "mode=");
    const char* action = strstr(line, "action=");
    const char* state = strstr(line, "state=");
    const char* connected = strstr(line, "connected=");
    const char* ssid = strstr(line, "ssid=");
    const char* reason = strstr(line, "reason=");

    if(mode && action) {
        mode += 5;
        size_t length = (size_t)(action - mode - 1);
        if(length >= sizeof(app->wifi_status_mode)) {
            length = sizeof(app->wifi_status_mode) - 1U;
        }
        memcpy(app->wifi_status_mode, mode, length);
        app->wifi_status_mode[length] = '\0';
    }

    if(action && state) {
        action += 7;
        size_t length = (size_t)(state - action - 1);
        if(length >= sizeof(app->wifi_status_action)) {
            length = sizeof(app->wifi_status_action) - 1U;
        }
        memcpy(app->wifi_status_action, action, length);
        app->wifi_status_action[length] = '\0';
    }

    if(state && connected) {
        state += 6;
        size_t length = (size_t)(connected - state - 1);
        if(length >= sizeof(app->wifi_status_state)) {
            length = sizeof(app->wifi_status_state) - 1U;
        }
        memcpy(app->wifi_status_state, state, length);
        app->wifi_status_state[length] = '\0';
    }

    if(connected) {
        app->wifi_connected = (strncmp(connected + 10, "yes", 3) == 0);
    }

    if(ssid && reason) {
        ssid += 5;
        size_t length = (size_t)(reason - ssid - 1);
        if(length >= sizeof(app->wifi_status_ssid)) {
            length = sizeof(app->wifi_status_ssid) - 1U;
        }
        memcpy(app->wifi_status_ssid, ssid, length);
        app->wifi_status_ssid[length] = '\0';
    }

    if(reason) {
        app->wifi_status_reason = (uint16_t)strtoul(reason + 7, NULL, 10);
    }

    if(strcmp(app->wifi_status_mode, "CONNECTED") == 0) {
        app->wifi_mode = FuseRadioWifiModeConnected;
    } else if(strcmp(app->wifi_status_mode, "PROMISCUOUS") == 0) {
        app->wifi_mode = FuseRadioWifiModePromiscuous;
    } else {
        app->wifi_mode = FuseRadioWifiModeIdle;
    }

    if(strcmp(app->wifi_status_action, "SCANNING") == 0) {
        app->wifi_action = FuseRadioWifiActionScanning;
    } else if(strcmp(app->wifi_status_action, "CONNECTING") == 0) {
        app->wifi_action = FuseRadioWifiActionConnecting;
    } else if(strcmp(app->wifi_status_action, "DISCONNECTING") == 0) {
        app->wifi_action = FuseRadioWifiActionDisconnecting;
    } else if(strcmp(app->wifi_status_action, "DISCOVERING") == 0) {
        app->wifi_action = FuseRadioWifiActionDiscovering;
    } else if(
        strcmp(app->wifi_status_action, "RESOLVING_MDNS") == 0 ||
        strcmp(app->wifi_status_action, "READING_MDNS") == 0) {
        app->wifi_action = FuseRadioWifiActionResolvingMdns;
    } else if(strcmp(app->wifi_status_action, "REQUESTING_HTTP") == 0) {
        app->wifi_action = FuseRadioWifiActionRequestingHttp;
    } else if(strcmp(app->wifi_status_action, "ENTERING_PROMISCUOUS") == 0) {
        app->wifi_action = FuseRadioWifiActionEnteringPromiscuous;
    } else if(strcmp(app->wifi_status_action, "EXITING_PROMISCUOUS") == 0) {
        app->wifi_action = FuseRadioWifiActionExitingPromiscuous;
    } else if(strcmp(app->wifi_status_action, "SURVEYING") == 0) {
        app->wifi_action = FuseRadioWifiActionSurveying;
    } else if(strcmp(app->wifi_status_action, "WATCHING") == 0) {
        app->wifi_action = FuseRadioWifiActionWatching;
    } else {
        app->wifi_action = FuseRadioWifiActionNone;
    }

    if(strcmp(app->wifi_status_state, "CONNECTED") == 0) {
        app->wifi_state = FuseRadioWifiStateConnected;
    } else if(strcmp(app->wifi_status_state, "CONNECTING") == 0) {
        app->wifi_state = FuseRadioWifiStateConnecting;
    } else if(strcmp(app->wifi_status_state, "SCANNING") == 0) {
        app->wifi_state = FuseRadioWifiStateScanning;
    } else {
        app->wifi_state = FuseRadioWifiStateIdle;
    }

    if(
        app->wifi_connected && app->connect_ssid[0] && app->connect_password[0] &&
        (previous_wifi_state == FuseRadioWifiStateConnectRequested ||
         previous_wifi_state == FuseRadioWifiStateConnecting ||
         app->current_request == FuseRadioRequestConnect)) {
        fuse_radio_app_store_saved_credential(app, app->connect_ssid, app->connect_password);
        app->skip_auto_password_ssid[0] = '\0';
        app->connect_password_saved = true;
        app->connect_password_auto_used = false;
        if(app->connect_flow_active) {
            app->connect_flow_active = false;
            view_dispatcher_send_custom_event(
                app->view_dispatcher, FuseRadioCustomEventWifiConnectedReady);
        }
    } else if(
        app->connect_password_auto_used && !app->wifi_connected &&
        previous_wifi_state == FuseRadioWifiStateConnecting &&
        strcmp(app->wifi_status_state, "CONNECTING") != 0) {
        fuse_radio_app_strlcpy(
            app->skip_auto_password_ssid,
            app->connect_ssid,
            sizeof(app->skip_auto_password_ssid));
        app->connect_password_auto_used = false;
        app->connect_password_saved = false;
        app->connect_flow_active = false;
    }

    fuse_radio_app_update_wifi_info_text(app);
}

static void fuse_radio_app_handle_error_line(FuseRadioApp* app, const char* line) {
    if(app->module_state != FuseRadioModuleStateDetected &&
       (strcmp(line, "ERR UNKNOWN_COMMAND") == 0 ||
        strcmp(line, "ERR UNKNOWN_WIFI_COMMAND") == 0)) {
        return;
    }

    if(app->current_request == FuseRadioRequestBleDistance || app->ble_distance_active) {
        app->current_request = FuseRadioRequestNone;
        app->ble_distance_active = false;
        app->ble_distance_stop_pending = false;
        app->ble_distance_has_error = true;
        fuse_radio_app_strlcpy(app->ble_distance_error, line, sizeof(app->ble_distance_error));
        app->ble_dirty = true;
        return;
    }

    if(app->current_request == FuseRadioRequestBleScan || app->ble_scan_results.active) {
        app->ble_scan_results.active = false;
        app->ble_scan_results.complete = true;
        app->ble_scan_results.has_error = true;
        app->ble_scan_results.progress_percent = 100U;
        strncpy(app->ble_scan_results.error, line, sizeof(app->ble_scan_results.error) - 1U);
        app->ble_scan_results.error[sizeof(app->ble_scan_results.error) - 1U] = '\0';
        app->current_request = FuseRadioRequestNone;
        app->ble_dirty = true;
        return;
    }

    if(app->current_request == FuseRadioRequestScan ||
       app->wifi_state == FuseRadioWifiStateScanning ||
       app->wifi_state == FuseRadioWifiStateScanRequested) {
        app->wifi_state = FuseRadioWifiStateError;
        app->scan_results.has_error = true;
        strncpy(app->scan_results.error, line, sizeof(app->scan_results.error) - 1U);
        app->scan_results.error[sizeof(app->scan_results.error) - 1U] = '\0';
        app->scan_results.is_complete = true;
        app->scan_dirty = true;
        app->current_request = FuseRadioRequestNone;
        return;
    }

    if(app->current_request == FuseRadioRequestMdns) {
        fuse_radio_app_reset_mdns_results(app);
        fuse_radio_app_append_mdns_text(app, "%s", line);
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        app->current_request = FuseRadioRequestNone;
        return;
    }

    if(app->current_request == FuseRadioRequestHttp) {
        fuse_radio_app_reset_http_results(app);
        fuse_radio_app_append_http_text(app, "%s", line);
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        app->current_request = FuseRadioRequestNone;
        return;
    }

    if(app->current_request == FuseRadioRequestDiscover) {
        fuse_radio_app_reset_discover_results(app);
        app->discover_results.has_error = true;
        fuse_radio_app_strlcpy(app->discover_results.error, line, sizeof(app->discover_results.error));
        app->discover_results.complete = true;
        app->discover_results.active = false;
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        app->current_request = FuseRadioRequestNone;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiDiscoverFailed);
        return;
    }

    if(app->current_request == FuseRadioRequestPromiscuousEnter ||
       app->current_request == FuseRadioRequestPromiscuousExit ||
       app->current_request == FuseRadioRequestPromiscuousSurvey ||
       app->current_request == FuseRadioRequestPromiscuousWatch) {
        const bool survey_failed = app->current_request == FuseRadioRequestPromiscuousSurvey;
        fuse_radio_app_reset_promiscuous_results(app);
        fuse_radio_app_append_promiscuous_text(app, "%s", line);
        if(app->current_request == FuseRadioRequestPromiscuousExit) {
            fuse_radio_app_set_wifi_mode_action(
                app, FuseRadioWifiModeIdle, "IDLE", FuseRadioWifiActionNone, "NONE");
        } else {
            fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        }
        app->current_request = FuseRadioRequestNone;
        if(survey_failed) {
            view_dispatcher_send_custom_event(
                app->view_dispatcher, FuseRadioCustomEventWifiSurveyFailed);
        }
        return;
    }

    if(app->current_request == FuseRadioRequestStatus ||
       app->current_request == FuseRadioRequestConnect ||
       app->current_request == FuseRadioRequestDisconnect) {
        if(app->connect_password_auto_used &&
           (app->current_request == FuseRadioRequestStatus ||
            app->current_request == FuseRadioRequestConnect)) {
            fuse_radio_app_strlcpy(
                app->skip_auto_password_ssid,
                app->connect_ssid,
                sizeof(app->skip_auto_password_ssid));
            app->connect_password_auto_used = false;
            app->connect_password_saved = false;
        }
        app->connect_flow_active = false;
        app->wifi_state = FuseRadioWifiStateError;
        fuse_radio_app_set_wifi_info_text(app, line);
        app->current_request = FuseRadioRequestNone;
        return;
    }

    if(app->current_request == FuseRadioRequestLedStatus ||
       app->current_request == FuseRadioRequestLedSet ||
       app->current_request == FuseRadioRequestLedAuto) {
        fuse_radio_app_set_status(app, line);
        app->current_request = FuseRadioRequestNone;
        return;
    }

    if(app->current_request == FuseRadioRequestConfigGet ||
       app->current_request == FuseRadioRequestConfigSet) {
        app->current_request = FuseRadioRequestNone;
        if(strcmp(line, "ERR CONFIG_NO_SD") == 0) {
            app->config_sd_available = false;
            view_dispatcher_send_custom_event(
                app->view_dispatcher, FuseRadioCustomEventConfigNoSd);
        } else {
            fuse_radio_app_strlcpy(
                app->config_info_text, line, sizeof(app->config_info_text));
            view_dispatcher_send_custom_event(
                app->view_dispatcher, FuseRadioCustomEventConfigFailed);
        }
        return;
    }

    if(strncmp(line, "ERR SD_", 7) == 0 || app->sd_action != FuseRadioSdActionNone) {
        fuse_radio_app_strlcpy(app->sd_info_text, line, sizeof(app->sd_info_text));
        app->sd_dirty = true;
        return;
    }

    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_error(app, line);
    }
}

static void fuse_radio_app_parse_led_status(FuseRadioApp* app, const char* line) {
    const char* mode = strstr(line, "mode=");
    const char* red = strstr(line, " red=");
    const char* green = strstr(line, " green=");
    const char* blue = strstr(line, " blue=");

    if(mode == NULL || red == NULL || green == NULL || blue == NULL) {
        return;
    }

    app->led_manual_override = strncmp(mode + 5, "MANUAL", 6) == 0;
    app->led_red = (uint8_t)strtoul(red + 5, NULL, 10);
    app->led_green = (uint8_t)strtoul(green + 7, NULL, 10);
    app->led_blue = (uint8_t)strtoul(blue + 6, NULL, 10);
}

static void fuse_radio_app_handle_line(FuseRadioApp* app, const char* line) {
    if(strcmp(line, "READY") == 0 || strcmp(line, "PONG") == 0) {
        fuse_radio_app_mark_detected(app);
    } else if(strncmp(line, "LED STATUS ", 11) == 0) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_parse_led_status(app, line);
    } else if(strncmp(line, "BLE_SCAN_START ", 15) == 0) {
        app->current_request = FuseRadioRequestBleScan;
        app->ble_scan_results.active = true;
        app->ble_scan_results.complete = false;
        app->ble_scan_results.has_error = false;
        app->ble_scan_results.progress_percent = 0U;
        app->ble_scan_results.scan_duration_ms = (uint16_t)strtoul(line + 15, NULL, 10);
        if(app->ble_scan_results.scan_duration_ms == 0U) {
            app->ble_scan_results.scan_duration_ms = fuse_radio_app_ble_scan_duration_ms(app);
        }
        app->ble_scan_started_at = furi_get_tick();
        app->ble_dirty = true;
    } else if(strncmp(line, "BLE_SCAN_COUNT ", 15) == 0) {
        app->current_request = FuseRadioRequestBleScan;
        app->ble_scan_results.total_count = (uint16_t)strtoul(line + 15, NULL, 10);
        app->ble_dirty = true;
    } else if(strncmp(line, "BLE_DEVICE ", 11) == 0) {
        fuse_radio_app_parse_ble_device_line(app, line);
    } else if(strncmp(line, "BLE_DISTANCE_STARTED ", 21) == 0) {
        app->current_request = FuseRadioRequestBleDistance;
        app->ble_distance_active = true;
        app->ble_distance_stop_pending = false;
        app->ble_distance_started_at = furi_get_tick();
        app->ble_distance_has_error = false;
        app->ble_dirty = true;
    } else if(strncmp(line, "BLE_DISTANCE_SAMPLE ", 20) == 0) {
        fuse_radio_app_parse_ble_distance_sample_line(app, line);
    } else if(strncmp(line, "BLE_DISTANCE_DONE", 17) == 0) {
        app->current_request = FuseRadioRequestNone;
        app->ble_distance_active = false;
        app->ble_distance_stop_pending = false;
        app->ble_dirty = true;
    } else if(strncmp(line, "BLE_SCAN_TRUNCATED ", 19) == 0) {
        app->ble_scan_results.truncated_count = (uint16_t)strtoul(line + 19, NULL, 10);
        app->ble_dirty = true;
    } else if(strcmp(line, "BLE_SCAN_DONE") == 0) {
        app->current_request = FuseRadioRequestNone;
        app->ble_scan_results.active = false;
        app->ble_scan_results.complete = true;
        app->ble_scan_results.progress_percent = 100U;
        app->ble_dirty = true;
    } else if(strncmp(line, "BLE_GATT_START ", 15) == 0) {
        app->current_request = FuseRadioRequestBleGatt;
        app->gatt_results.active = true;
        app->gatt_results.complete = false;
        app->gatt_results.has_error = false;
        app->gatt_results.svc_count = 0U;
        fuse_radio_app_strlcpy(app->gatt_results.mac, line + 15, sizeof(app->gatt_results.mac));
        app->gatt_dirty = true;
    } else if(strcmp(line, "BLE_GATT_CONNECTED") == 0) {
        app->gatt_dirty = true;
    } else if(strncmp(line, "BLE_GATT_SVC ", 13) == 0) {
        /* BLE_GATT_SVC <uuid> NAME <name> */
        if(app->gatt_results.svc_count < FUSE_RADIO_MAX_GATT_SVCS) {
            const char* uuid_begin = line + 13;
            const char* name_tag = strstr(uuid_begin, " NAME ");
            FuseRadioGattService* svc = &app->gatt_results.svcs[app->gatt_results.svc_count];
            if(name_tag != NULL) {
                const size_t uuid_len = (size_t)(name_tag - uuid_begin);
                const size_t copy_uuid = uuid_len < sizeof(svc->uuid) - 1U ?
                                            uuid_len :
                                            sizeof(svc->uuid) - 1U;
                memcpy(svc->uuid, uuid_begin, copy_uuid);
                svc->uuid[copy_uuid] = '\0';
                fuse_radio_app_strlcpy(svc->name, name_tag + 6, sizeof(svc->name));
            } else {
                fuse_radio_app_strlcpy(svc->uuid, uuid_begin, sizeof(svc->uuid));
                svc->name[0] = '-';
                svc->name[1] = '\0';
            }
            svc->chr_count = 0U;
            app->gatt_results.svc_count++;
        }
        app->gatt_dirty = true;
    } else if(strncmp(line, "BLE_GATT_CHR ", 13) == 0) {
        /* BLE_GATT_CHR <svc_uuid> <chr_uuid> PROPS <props> NAME <name> */
        const char* svc_uuid_begin = line + 13;
        const char* chr_uuid_begin = strstr(svc_uuid_begin, " ");
        const char* props_tag = chr_uuid_begin ? strstr(chr_uuid_begin + 1, " PROPS ") : NULL;
        const char* name_tag = props_tag ? strstr(props_tag + 7, " NAME ") : NULL;
        if(chr_uuid_begin != NULL && props_tag != NULL && name_tag != NULL &&
           app->gatt_results.svc_count > 0U) {
            /* Find matching service */
            char svc_uuid[FUSE_RADIO_GATT_UUID_SIZE];
            const size_t svc_uuid_len = (size_t)(chr_uuid_begin - svc_uuid_begin);
            const size_t copy_len = svc_uuid_len < sizeof(svc_uuid) - 1U ?
                                        svc_uuid_len :
                                        sizeof(svc_uuid) - 1U;
            memcpy(svc_uuid, svc_uuid_begin, copy_len);
            svc_uuid[copy_len] = '\0';
            /* Walk backwards to find the right service */
            FuseRadioGattService* svc = NULL;
            for(int32_t si = (int32_t)app->gatt_results.svc_count - 1; si >= 0; si--) {
                if(strcmp(app->gatt_results.svcs[si].uuid, svc_uuid) == 0) {
                    svc = &app->gatt_results.svcs[si];
                    break;
                }
            }
            if(svc != NULL && svc->chr_count < FUSE_RADIO_MAX_GATT_CHRS_PER_SVC) {
                FuseRadioGattCharacteristic* chr = &svc->chrs[svc->chr_count];
                const char* chr_uuid_end = props_tag;
                const size_t chr_uuid_len = (size_t)(chr_uuid_end - (chr_uuid_begin + 1));
                const size_t copy_chr = chr_uuid_len < sizeof(chr->uuid) - 1U ?
                                            chr_uuid_len :
                                            sizeof(chr->uuid) - 1U;
                memcpy(chr->uuid, chr_uuid_begin + 1, copy_chr);
                chr->uuid[copy_chr] = '\0';
                const size_t props_len = (size_t)(name_tag - (props_tag + 7));
                const size_t copy_props = props_len < sizeof(chr->props) - 1U ?
                                              props_len :
                                              sizeof(chr->props) - 1U;
                memcpy(chr->props, props_tag + 7, copy_props);
                chr->props[copy_props] = '\0';
                fuse_radio_app_strlcpy(chr->name, name_tag + 6, sizeof(chr->name));
                chr->has_value = false;
                chr->value[0] = '\0';
                chr->has_raw = false;
                chr->raw[0] = '\0';
                svc->chr_count++;
            }
        }
        app->gatt_dirty = true;
    } else if(strncmp(line, "BLE_GATT_VAL ", 13) == 0) {
        /* BLE_GATT_VAL <svc_uuid> <chr_uuid> <value> */
        const char* svc_uuid_begin = line + 13;
        const char* chr_uuid_begin = strstr(svc_uuid_begin, " ");
        const char* value_begin = chr_uuid_begin ? strstr(chr_uuid_begin + 1, " ") : NULL;
        if(chr_uuid_begin != NULL && value_begin != NULL) {
            char svc_uuid[FUSE_RADIO_GATT_UUID_SIZE];
            char chr_uuid[FUSE_RADIO_GATT_UUID_SIZE];

            const size_t svc_uuid_len = (size_t)(chr_uuid_begin - svc_uuid_begin);
            const size_t copy_svc = svc_uuid_len < sizeof(svc_uuid) - 1U ?
                                        svc_uuid_len :
                                        sizeof(svc_uuid) - 1U;
            memcpy(svc_uuid, svc_uuid_begin, copy_svc);
            svc_uuid[copy_svc] = '\0';

            const size_t chr_uuid_len = (size_t)(value_begin - (chr_uuid_begin + 1));
            const size_t copy_chr = chr_uuid_len < sizeof(chr_uuid) - 1U ?
                                        chr_uuid_len :
                                        sizeof(chr_uuid) - 1U;
            memcpy(chr_uuid, chr_uuid_begin + 1, copy_chr);
            chr_uuid[copy_chr] = '\0';

            for(uint8_t si = 0; si < app->gatt_results.svc_count; si++) {
                FuseRadioGattService* svc = &app->gatt_results.svcs[si];
                if(strcmp(svc->uuid, svc_uuid) != 0) {
                    continue;
                }

                for(uint8_t ci = 0; ci < svc->chr_count; ci++) {
                    if(strcmp(svc->chrs[ci].uuid, chr_uuid) == 0) {
                        fuse_radio_app_strlcpy(
                            svc->chrs[ci].value,
                            value_begin + 1,
                            sizeof(svc->chrs[ci].value));
                        svc->chrs[ci].has_value = true;
                        break;
                    }
                }
            }
        }
        app->gatt_dirty = true;
    } else if(strncmp(line, "BLE_GATT_RAW ", 13) == 0) {
        /* BLE_GATT_RAW <svc_uuid> <chr_uuid> <raw_hex> */
        const char* svc_uuid_begin = line + 13;
        const char* chr_uuid_begin = strstr(svc_uuid_begin, " ");
        const char* raw_begin = chr_uuid_begin ? strstr(chr_uuid_begin + 1, " ") : NULL;
        if(chr_uuid_begin != NULL && raw_begin != NULL) {
            char svc_uuid[FUSE_RADIO_GATT_UUID_SIZE];
            char chr_uuid[FUSE_RADIO_GATT_UUID_SIZE];

            const size_t svc_uuid_len = (size_t)(chr_uuid_begin - svc_uuid_begin);
            const size_t copy_svc = svc_uuid_len < sizeof(svc_uuid) - 1U ?
                                        svc_uuid_len :
                                        sizeof(svc_uuid) - 1U;
            memcpy(svc_uuid, svc_uuid_begin, copy_svc);
            svc_uuid[copy_svc] = '\0';

            const size_t chr_uuid_len = (size_t)(raw_begin - (chr_uuid_begin + 1));
            const size_t copy_chr = chr_uuid_len < sizeof(chr_uuid) - 1U ?
                                        chr_uuid_len :
                                        sizeof(chr_uuid) - 1U;
            memcpy(chr_uuid, chr_uuid_begin + 1, copy_chr);
            chr_uuid[copy_chr] = '\0';

            for(uint8_t si = 0; si < app->gatt_results.svc_count; si++) {
                FuseRadioGattService* svc = &app->gatt_results.svcs[si];
                if(strcmp(svc->uuid, svc_uuid) != 0) {
                    continue;
                }

                for(uint8_t ci = 0; ci < svc->chr_count; ci++) {
                    if(strcmp(svc->chrs[ci].uuid, chr_uuid) == 0) {
                        fuse_radio_app_strlcpy(
                            svc->chrs[ci].raw,
                            raw_begin + 1,
                            sizeof(svc->chrs[ci].raw));
                        svc->chrs[ci].has_raw = true;
                        break;
                    }
                }
            }
        }
        app->gatt_dirty = true;
    } else if(strcmp(line, "BLE_GATT_DONE") == 0) {
        app->current_request = FuseRadioRequestNone;
        app->gatt_results.active = false;
        app->gatt_results.complete = true;
        app->gatt_dirty = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventBleGattDone);
    } else if(strncmp(line, "BLE_GATT_CONNECT_FAILED", 23) == 0 ||
              strncmp(line, "BLE_GATT_DISCOVER_FAILED", 24) == 0) {
        app->current_request = FuseRadioRequestNone;
        app->gatt_results.active = false;
        app->gatt_results.complete = true;
        app->gatt_results.has_error = true;
        const bool connect_failed = strncmp(line, "BLE_GATT_CONNECT_FAILED", 23) == 0;
        const char* detail = strchr(line, ' ');
        if(detail != NULL && detail[1] != '\0') {
            snprintf(
                app->gatt_results.error,
                sizeof(app->gatt_results.error),
                "%s: %s",
                connect_failed ? "Connect" : "Discover",
                detail + 1);
        } else {
            strncpy(
                app->gatt_results.error,
                connect_failed ? "Connect failed" : "Discover failed",
                sizeof(app->gatt_results.error) - 1U);
        }
        app->gatt_results.error[sizeof(app->gatt_results.error) - 1U] = '\0';
        app->gatt_dirty = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventBleGattFailed);
    } else if(strncmp(line, "SCAN_COUNT ", 11) == 0) {
        app->current_request = FuseRadioRequestScan;
        app->scan_results.total_count = (uint16_t)strtoul(line + 11, NULL, 10);
        app->wifi_state = FuseRadioWifiStateScanning;
        app->scan_dirty = true;
    } else if(strncmp(line, "AP ", 3) == 0) {
        fuse_radio_app_parse_ap_line(app, line);
    } else if(strncmp(line, "SCAN_TRUNCATED ", 15) == 0) {
        app->scan_results.truncated_count = (uint16_t)strtoul(line + 15, NULL, 10);
        app->scan_dirty = true;
    } else if(strcmp(line, "SCAN_DONE") == 0) {
        app->current_request = FuseRadioRequestNone;
        app->wifi_state = FuseRadioWifiStateScanComplete;
        app->scan_results.is_complete = true;
        app->scan_dirty = true;
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
    } else if(strcmp(line, "WIFI CONNECTING") == 0) {
        app->current_request = FuseRadioRequestNone;
        app->wifi_state = FuseRadioWifiStateConnecting;
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionConnecting, "CONNECTING");
        snprintf(
            app->wifi_info_text,
            sizeof(app->wifi_info_text),
            "Entering connected mode via AP join.\nSSID: %s\nUse Refresh to poll status.",
            app->connect_ssid);
        app->wifi_info_dirty = true;
    } else if(strcmp(line, "WIFI DISCONNECTING") == 0) {
        app->current_request = FuseRadioRequestNone;
        app->wifi_state = FuseRadioWifiStateDisconnecting;
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionDisconnecting, "DISCONNECTING");
        fuse_radio_app_set_wifi_info_text(app, "Disconnecting...\nUse Refresh to confirm state.");
    } else if(strcmp(line, "WIFI PROMISCUOUS ENTERED") == 0) {
            const bool survey_pending = app->current_request == FuseRadioRequestPromiscuousSurvey;
            const bool watch_pending = app->current_request == FuseRadioRequestPromiscuousWatch;

            if(!survey_pending && !watch_pending) {
                app->current_request = FuseRadioRequestNone;
            }

            if(survey_pending) {
                fuse_radio_app_set_wifi_mode_action(
                    app,
                    FuseRadioWifiModePromiscuous,
                    "PROMISCUOUS",
                    FuseRadioWifiActionSurveying,
                    "SURVEYING");
            } else if(watch_pending) {
                fuse_radio_app_set_wifi_mode_action(
                    app,
                    FuseRadioWifiModePromiscuous,
                    "PROMISCUOUS",
                    FuseRadioWifiActionWatching,
                    "WATCHING");
            } else {
                fuse_radio_app_set_wifi_mode_action(
                    app,
                    FuseRadioWifiModePromiscuous,
                    "PROMISCUOUS",
                    FuseRadioWifiActionNone,
                    "NONE");
            }
        app->wifi_connected = false;
            if(survey_pending) {
                fuse_radio_app_append_promiscuous_text(
                    app, "Promiscuous mode is active.\nStarting survey...\n");
            } else if(watch_pending) {
                fuse_radio_app_append_promiscuous_text(
                    app, "Promiscuous mode is active.\nStarting watch...\n");
            } else {
                fuse_radio_app_append_promiscuous_text(
                    app, "Promiscuous mode is active.\nPick a survey or watch action.\n");
            }
    } else if(strcmp(line, "WIFI PROMISCUOUS EXITED") == 0) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_set_wifi_mode_action(
            app, FuseRadioWifiModeIdle, "IDLE", FuseRadioWifiActionNone, "NONE");
        app->wifi_connected = false;
        fuse_radio_app_append_promiscuous_text(app, "Returned to idle WiFi mode.\n");
    } else if(strncmp(line, "WIFI STATUS ", 12) == 0) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_parse_wifi_status(app, line);
    } else if(strncmp(line, "HTTP_LINE ", 10) == 0) {
        fuse_radio_app_parse_http_line(app, line);
    } else if(strcmp(line, "HTTP_DONE") == 0) {
        if(app->http_info_text[0] == '\0') {
            fuse_radio_app_append_http_text(app, "No HTTP response data.\n");
        }
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        app->current_request = FuseRadioRequestNone;
    } else if(strncmp(line, "MDNS_COUNT ", 11) == 0) {
        app->mdns_count = (uint8_t)strtoul(line + 11, NULL, 10);
        fuse_radio_app_reset_mdns_results(app);
        fuse_radio_app_append_mdns_text(app, "Results: %u\n\n", (unsigned)app->mdns_count);
    } else if(strncmp(line, "DISCOVER_NETWORK ", 17) == 0) {
        fuse_radio_app_parse_discover_network_line(app, line);
    } else if(strncmp(line, "DISCOVER_PROGRESS ", 18) == 0) {
        fuse_radio_app_parse_discover_progress_line(app, line);
    } else if(strncmp(line, "DISCOVER_FOUND ", 15) == 0) {
        fuse_radio_app_parse_discover_found_line(app, line);
    } else if(strncmp(line, "DISCOVER_DONE ", 14) == 0) {
        const char* scanned = strstr(line, "scanned=");
        const char* found = strstr(line, " found=");
        const char* duration = strstr(line, " duration_ms=");

        if(scanned) {
            app->discover_results.scanned_count = (uint16_t)strtoul(scanned + 8, NULL, 10);
        }
        if(found) {
            app->discover_results.found_count = (uint16_t)strtoul(found + 7, NULL, 10);
        }
        if(duration) {
            app->discover_results.duration_ms = (uint32_t)strtoul(duration + 13, NULL, 10);
        }

        app->discover_results.progress_percent = 100U;
        app->discover_results.complete = true;
        app->discover_results.active = false;
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        app->current_request = FuseRadioRequestNone;
        app->discover_dirty = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiDiscoverDone);
    } else if(strncmp(line, "MDNS host=", 10) == 0) {
        fuse_radio_app_parse_mdns_line(app, line);
    } else if(strncmp(line, "MDNS_TRUNCATED ", 15) == 0) {
        app->mdns_truncated_count = (uint8_t)strtoul(line + 15, NULL, 10);
        fuse_radio_app_append_mdns_text(
            app, "+%u more results omitted\n", (unsigned)app->mdns_truncated_count);
    } else if(strcmp(line, "MDNS_DONE") == 0) {
        if(app->mdns_count == 0U && app->mdns_info_text[0] == '\0') {
            fuse_radio_app_append_mdns_text(app, "No mDNS results.");
        }
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        app->current_request = FuseRadioRequestNone;
    } else if(strncmp(line, "SURVEY channel=", 15) == 0) {
        const char* channel = strstr(line, "channel=");
        const char* total = strstr(line, " total=");
        const char* mgmt = strstr(line, " mgmt=");
        const char* data = strstr(line, " data=");
        const char* ctrl = strstr(line, " ctrl=");
        const char* misc = strstr(line, " misc=");
        const char* unique = strstr(line, " unique=");
        const char* max_rssi = strstr(line, " max_rssi=");
        const char* beacons = strstr(line, " beacons=");
        const char* deauth = strstr(line, " deauth=");
        const char* duration = strstr(line, " duration_ms=");

        if(channel && total && mgmt && data && ctrl && misc && unique && max_rssi && beacons &&
           deauth && duration) {
            unsigned long channel_value = strtoul(channel + 8, NULL, 10);
            uint8_t result_index = app->survey_results.completed_count;

            if(result_index < app->survey_results.expected_count) {
                FuseRadioSurveyChannelResult* result = &app->survey_results.channels[result_index];
                result->channel = (uint8_t)channel_value;
                result->total_frames = (uint32_t)strtoul(total + 7, NULL, 10);
                result->management_frames = (uint32_t)strtoul(mgmt + 6, NULL, 10);
                result->data_frames = (uint32_t)strtoul(data + 6, NULL, 10);
                result->control_frames = (uint32_t)strtoul(ctrl + 6, NULL, 10);
                result->misc_frames = (uint32_t)strtoul(misc + 6, NULL, 10);
                result->beacon_frames = (uint16_t)strtoul(beacons + 9, NULL, 10);
                result->deauth_frames = (uint16_t)strtoul(deauth + 8, NULL, 10);
                result->unique_count = (uint16_t)strtoul(unique + 8, NULL, 10);
                result->max_rssi = (int16_t)strtol(max_rssi + 10, NULL, 10);
                result->duration_ms = (uint32_t)strtoul(duration + 13, NULL, 10);
                result->has_result = true;
                app->survey_results.result_count = result_index + 1U;
                app->survey_results.completed_count = result_index + 1U;
                app->survey_results.progress_percent =
                    (uint8_t)((app->survey_results.completed_count * 100U) /
                              app->survey_results.expected_count);
                if(app->survey_results.completed_count < app->survey_results.expected_count) {
                    app->survey_results.current_channel =
                        app->survey_results.channels[app->survey_results.completed_count].channel;
                }
            }

            fuse_radio_app_append_promiscuous_text(
                app,
                "CH %lu\nFrames: %lu\nUnique: %lu\nBeacons: %lu\nMax RSSI: %ld dBm\n\n",
                channel_value,
                strtoul(total + 7, NULL, 10),
                strtoul(unique + 8, NULL, 10),
                strtoul(beacons + 9, NULL, 10),
                strtol(max_rssi + 10, NULL, 10));
            app->promiscuous_dirty = true;
        } else {
            fuse_radio_app_append_promiscuous_text(app, "%s\n\n", line);
        }
    } else if(strncmp(line, "SURVEY_DONE ", 12) == 0) {
        const char* channels = strstr(line, "channels=");
        const char* dwell = strstr(line, " dwell_ms=");
        const char* duration = strstr(line, " duration_ms=");
        const char* recommended = strstr(line, " recommended=");
        unsigned long channel_count = channels ? strtoul(channels + 9, NULL, 10) : 0UL;
        unsigned long recommended_channel = recommended ? strtoul(recommended + 13, NULL, 10) : 0UL;
        app->survey_results.result_count = (uint8_t)channel_count;
        app->survey_results.completed_count = (uint8_t)channel_count;
        app->survey_results.recommended_channel = (uint8_t)recommended_channel;
        app->survey_results.progress_percent = 100U;
        app->survey_results.active = false;
        app->survey_results.complete = true;
        if(dwell) {
            app->survey_results.dwell_ms = (uint16_t)strtoul(dwell + 10, NULL, 10);
        }
        if(duration) {
            app->survey_results.duration_ms = (uint32_t)strtoul(duration + 13, NULL, 10);
        }
        fuse_radio_app_append_promiscuous_text(
            app,
            "Survey complete\nChannels: %lu\nRecommended: %lu\n",
            channel_count,
            recommended_channel);
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_set_wifi_mode_action(
            app,
            FuseRadioWifiModePromiscuous,
            "PROMISCUOUS",
            FuseRadioWifiActionNone,
            "NONE");
        app->promiscuous_dirty = true;
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventWifiSurveyDone);
    } else if(strncmp(line, "WATCH_STARTED ", 14) == 0) {
        const char* channel = strstr(line, "channel=");
        unsigned long channel_value = channel ? strtoul(channel + 8, NULL, 10) : 0UL;

        app->promiscuous_watch_channel = (uint8_t)channel_value;
        app->promiscuous_live_elapsed_ms = 0U;
        app->promiscuous_live_total_frames = 0U;
        app->promiscuous_live_unique_count = 0U;
        app->promiscuous_live_beacon_count = 0U;
        app->promiscuous_live_max_rssi = -127;
        app->promiscuous_watch_live_active = true;
        app->promiscuous_watch_stop_pending = false;
        fuse_radio_app_set_promiscuous_live_text(
            app,
            "Ch %lu\nLive watch\n\nFrames: 0\nUnique: 0\nBeacons: 0\nRSSI: -- dBm",
            channel_value);
    } else if(strncmp(line, "WATCH_LIVE ", 11) == 0) {
        const char* channel = strstr(line, "channel=");
        const char* elapsed = strstr(line, " elapsed_ms=");
        const char* total = strstr(line, " total=");
        const char* unique = strstr(line, " unique=");
        const char* beacons = strstr(line, " beacons=");
        const char* max_rssi = strstr(line, " max_rssi=");

        if(channel && elapsed && total && unique && beacons && max_rssi) {
            unsigned long channel_value = strtoul(channel + 8, NULL, 10);
            unsigned long elapsed_value = strtoul(elapsed + 12, NULL, 10);
            unsigned long total_value = strtoul(total + 7, NULL, 10);
            unsigned long unique_value = strtoul(unique + 8, NULL, 10);
            unsigned long beacon_value = strtoul(beacons + 9, NULL, 10);
            long max_rssi_value = strtol(max_rssi + 10, NULL, 10);

            app->promiscuous_watch_channel = (uint8_t)channel_value;
            app->promiscuous_live_elapsed_ms = (uint32_t)elapsed_value;
            app->promiscuous_live_total_frames = (uint32_t)total_value;
            app->promiscuous_live_unique_count = (uint16_t)unique_value;
            app->promiscuous_live_beacon_count = (uint16_t)beacon_value;
            app->promiscuous_live_max_rssi = (int16_t)max_rssi_value;
            app->promiscuous_watch_live_active = true;
            fuse_radio_app_set_promiscuous_live_text(
                app,
                "Ch %lu\n%lus elapsed\n\nFrames: %lu\nUnique: %lu\nBeacons: %lu\nRSSI: %ld dBm",
                channel_value,
                elapsed_value / 1000UL,
                total_value,
                unique_value,
                beacon_value,
                max_rssi_value);
        }
    } else if(strncmp(line, "WATCH_SUMMARY ", 14) == 0) {
        const char* channel = strstr(line, "channel=");
        const char* duration = strstr(line, " duration_ms=");
        const char* total = strstr(line, " total=");
        const char* mgmt = strstr(line, " mgmt=");
        const char* data = strstr(line, " data=");
        const char* ctrl = strstr(line, " ctrl=");
        const char* misc = strstr(line, " misc=");
        const char* beacons = strstr(line, " beacons=");
        const char* deauth = strstr(line, " deauth=");
        const char* unique = strstr(line, " unique=");
        const char* max_rssi = strstr(line, " max_rssi=");

        if(channel && duration && total && mgmt && data && ctrl && misc && beacons && deauth &&
           unique && max_rssi) {
            fuse_radio_app_reset_promiscuous_results(app);
            app->watch_summary.channel = (uint8_t)strtoul(channel + 8, NULL, 10);
            app->watch_summary.duration_ms = (uint32_t)strtoul(duration + 13, NULL, 10);
            app->watch_summary.total_frames = (uint32_t)strtoul(total + 7, NULL, 10);
            app->watch_summary.management_frames = (uint32_t)strtoul(mgmt + 6, NULL, 10);
            app->watch_summary.data_frames = (uint32_t)strtoul(data + 6, NULL, 10);
            app->watch_summary.control_frames = (uint32_t)strtoul(ctrl + 6, NULL, 10);
            app->watch_summary.misc_frames = (uint32_t)strtoul(misc + 6, NULL, 10);
            app->watch_summary.beacon_frames = (uint16_t)strtoul(beacons + 9, NULL, 10);
            app->watch_summary.deauth_frames = (uint16_t)strtoul(deauth + 8, NULL, 10);
            app->watch_summary.unique_count = (uint16_t)strtoul(unique + 8, NULL, 10);
            app->watch_summary.max_rssi = (int16_t)strtol(max_rssi + 10, NULL, 10);
            app->watch_summary.has_summary = true;
            fuse_radio_app_append_promiscuous_text(
                app,
                "Watch complete\nChannel: %lu\nDuration: %lus\nFrames: %lu\nMgmt: %lu\nData: %lu\nCtrl: %lu\nMisc: %lu\nBeacons: %lu\nDeauth: %lu\nUnique: %lu\nMax RSSI: %ld dBm\n\nDevices\n",
                strtoul(channel + 8, NULL, 10),
                strtoul(duration + 13, NULL, 10) / 1000UL,
                strtoul(total + 7, NULL, 10),
                strtoul(mgmt + 6, NULL, 10),
                strtoul(data + 6, NULL, 10),
                strtoul(ctrl + 6, NULL, 10),
                strtoul(misc + 6, NULL, 10),
                strtoul(beacons + 9, NULL, 10),
                strtoul(deauth + 8, NULL, 10),
                strtoul(unique + 8, NULL, 10),
                strtol(max_rssi + 10, NULL, 10));
        }
    } else if(strncmp(line, "WATCH_DEVICE ", 13) == 0) {
        const char* mac = strstr(line, "mac=");
        const char* frames = strstr(line, " frames=");
        const char* beacons = strstr(line, " beacons=");
        const char* max_rssi = strstr(line, " max_rssi=");

        if(mac && frames) {
            char address[18] = {0};
            size_t address_length = (size_t)(frames - (mac + 4));

            if(address_length >= sizeof(address)) {
                address_length = sizeof(address) - 1U;
            }

            memcpy(address, mac + 4, address_length);
            address[address_length] = '\0';

            if(app->watch_device_count < FUSE_RADIO_MAX_WATCH_DEVICES) {
                FuseRadioWatchDevice* device = &app->watch_devices[app->watch_device_count++];
                strncpy(device->mac, address, sizeof(device->mac) - 1U);
                device->mac[sizeof(device->mac) - 1U] = '\0';
                device->frames = (uint32_t)strtoul(frames + 8, NULL, 10);
                device->beacons =
                    beacons ? (uint16_t)strtoul(beacons + 9, NULL, 10) : 0U;
                device->max_rssi =
                    max_rssi ? (int16_t)strtol(max_rssi + 10, NULL, 10) : -127;
            }

            fuse_radio_app_update_watch_device_roles(app);

            fuse_radio_app_append_promiscuous_text(
                app,
                "%s  %lu  b%u\n",
                address,
                strtoul(frames + 8, NULL, 10),
                beacons ? strtoul(beacons + 9, NULL, 10) : 0UL);
        }
    } else if(strncmp(line, "WATCH_DONE ", 11) == 0) {
        const char* channel = strstr(line, "channel=");
        unsigned long channel_value = channel ? strtoul(channel + 8, NULL, 10) : 0UL;
        if(app->promiscuous_info_text[0] == '\0') {
            fuse_radio_app_append_promiscuous_text(
                app, "Watch complete\nChannel: %lu\n", channel_value);
        }
        app->promiscuous_watch_live_active = false;
        app->promiscuous_watch_stop_pending = false;
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_set_wifi_mode_action(
            app,
            FuseRadioWifiModePromiscuous,
            "PROMISCUOUS",
            FuseRadioWifiActionNone,
            "NONE");
    } else if(strncmp(line, "BEACON_STARTED ", 15) == 0) {
        const char* channel = strstr(line, "channel=");
        const char* ssids = strstr(line, " ssids=");
        const char* duration = strstr(line, " duration_ms=");
        unsigned long ch = channel ? strtoul(channel + 8, NULL, 10) : 0UL;
        unsigned long ssid_count = ssids ? strtoul(ssids + 7, NULL, 10) : 0UL;
        unsigned long dur_ms = duration ? strtoul(duration + 13, NULL, 10) : 0UL;

        app->beacon_active = true;
        app->beacon_stop_pending = false;
        snprintf(
            app->beacon_info_text,
            sizeof(app->beacon_info_text),
            "Ch %lu  %lus\n%lu SSIDs active\nBack or OK to stop",
            ch,
            dur_ms / 1000UL,
            ssid_count);
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionBeaconing, "BEACONING");
        app->promiscuous_dirty = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiBeaconStarted);
    } else if(strncmp(line, "BEACON_DONE ", 12) == 0) {
        const char* channel = strstr(line, "channel=");
        const char* ssids = strstr(line, " ssids=");
        const char* duration = strstr(line, " duration_ms=");
        unsigned long ch = channel ? strtoul(channel + 8, NULL, 10) : 0UL;
        unsigned long ssid_count = ssids ? strtoul(ssids + 7, NULL, 10) : 0UL;
        unsigned long dur_ms = duration ? strtoul(duration + 13, NULL, 10) : 0UL;

        app->beacon_active = false;
        app->beacon_stop_pending = false;
        app->current_request = FuseRadioRequestNone;
        snprintf(
            app->beacon_info_text,
            sizeof(app->beacon_info_text),
            "Ch %lu  %lus\n%lu SSIDs broadcast\n\nDone",
            ch,
            dur_ms / 1000UL,
            ssid_count);
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        app->promiscuous_dirty = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiBeaconDone);
    } else if(strncmp(line, "SD INFO ", 8) == 0) {
        fuse_radio_app_parse_sd_info(app, line);
    } else if(strncmp(line, "SD LIST_BEGIN ", 14) == 0) {
        const char* path = strstr(line, "path=");
        app->sd_entry_count = 0U;
        if(path != NULL) {
            fuse_radio_app_strlcpy(app->sd_explore_path, path + 5, sizeof(app->sd_explore_path));
        }
        snprintf(
            app->sd_info_text,
            sizeof(app->sd_info_text),
            "Path: %s\n\nReading directory...",
            app->sd_explore_path[0] ? app->sd_explore_path : "/");
        app->sd_dirty = true;
    } else if(strncmp(line, "SD ENTRY ", 9) == 0) {
        fuse_radio_app_parse_sd_entry(app, line);
    } else if(strncmp(line, "SD LIST_DONE ", 13) == 0) {
        const char* count = strstr(line, "count=");
        const char* total = strstr(line, " total=");
        const char* truncated = strstr(line, " truncated=");
        unsigned long count_value = count ? strtoul(count + 6, NULL, 10) : 0UL;
        unsigned long total_value = total ? strtoul(total + 7, NULL, 10) : 0UL;
        unsigned long truncated_value = truncated ? strtoul(truncated + 11, NULL, 10) : 0UL;

        snprintf(
            app->sd_info_text,
            sizeof(app->sd_info_text),
            "Path: %s\nEntries: %lu/%lu\nTruncated: %lu",
            app->sd_explore_path[0] ? app->sd_explore_path : "/",
            count_value,
            total_value,
            truncated_value);
        app->sd_dirty = true;
    } else if(strcmp(line, "SD FORMAT_START") == 0) {
        snprintf(app->sd_info_text, sizeof(app->sd_info_text), "Formatting card...\n");
        app->sd_action = FuseRadioSdActionFormat;
        app->sd_dirty = true;
    } else if(strncmp(line, "SD FORMAT_PROGRESS ", 19) == 0) {
        const char* pct = strstr(line, "pct=");
        const char* step = strstr(line, " step=");
        unsigned long pct_value = pct ? strtoul(pct + 4, NULL, 10) : 0UL;

        snprintf(
            app->sd_info_text,
            sizeof(app->sd_info_text),
            "Formatting card...\nProgress: %lu%%\nStep: %s",
            pct_value,
            step ? (step + 6) : "-");
        app->sd_action = FuseRadioSdActionFormat;
        app->sd_dirty = true;
    } else if(strncmp(line, "SD FORMAT_DONE ", 15) == 0) {
        app->sd_confirm_format = false;
        fuse_radio_app_start_sd_action(app, FuseRadioSdActionDetail);
    } else if(strncmp(line, "CONFIG_START ", 13) == 0) {
        app->config_entry_count  = 0U;
        app->config_sd_available = true;
        app->config_dirty        = false;
    } else if(strcmp(line, "CONFIG_SD YES") == 0) {
        app->config_sd_available = true;
    } else if(strcmp(line, "CONFIG_SD NO") == 0) {
        app->config_sd_available = false;
    } else if(strncmp(line, "CONFIG_ENTRY ", 13) == 0) {
        fuse_radio_app_parse_config_entry(app, line);
    } else if(strcmp(line, "CONFIG_DONE") == 0) {
        app->current_request = FuseRadioRequestNone;
        app->config_dirty    = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventConfigLoaded);
    } else if(strncmp(line, "CONFIG_SET_OK ", 14) == 0) {
        app->current_request = FuseRadioRequestNone;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventConfigSetDone);
    } else if(strcmp(line, "CONFIG_RESET_OK") == 0) {
        app->current_request = FuseRadioRequestNone;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventConfigSetDone);
    } else if(strncmp(line, "ERR ", 4) == 0) {
        fuse_radio_app_handle_error_line(app, line);
    }
}

void fuse_radio_app_process_rx(FuseRadioApp* app) {
    uint8_t byte = 0;

    while(furi_stream_buffer_receive(app->rx_stream, &byte, sizeof(byte), 0U) == sizeof(byte)) {
        if(byte == '\r') {
            continue;
        }

        if(byte == '\n') {
            if(!app->line_overflow && app->line_length > 0U) {
                app->line_buffer[app->line_length] = '\0';
                fuse_radio_app_handle_line(app, app->line_buffer);
            }

            app->line_length = 0U;
            app->line_overflow = false;
            continue;
        }

        if(app->line_length >= FUSE_RADIO_MAX_LINE_LENGTH) {
            app->line_overflow = true;
            continue;
        }

        app->line_buffer[app->line_length++] = (char)byte;
    }
}

bool fuse_radio_app_start_session(FuseRadioApp* app) {
    furi_assert(app);

    return fuse_radio_app_bootstrap_session(app, true);
}

void fuse_radio_app_stop_session(FuseRadioApp* app) {
    furi_assert(app);

    if(app->ble_distance_active && app->serial_handle) {
        (void)fuse_radio_app_send_ble_distance_stop(app);
    }

    if(app->rx_started && app->serial_handle) {
        furi_hal_serial_async_rx_stop(app->serial_handle);
        app->rx_started = false;
    }

    if(app->serial_handle) {
        furi_hal_serial_deinit(app->serial_handle);
        furi_hal_serial_control_release(app->serial_handle);
        app->serial_handle = NULL;
    }

    fuse_radio_app_disable_otg(app);

    if(app->insomnia_active) {
        furi_hal_power_insomnia_exit();
        app->insomnia_active = false;
    }

    if(app->expansion && app->expansion_disabled) {
        expansion_enable(app->expansion);
        app->expansion_disabled = false;
    }

    if(app->expansion) {
        furi_record_close(RECORD_EXPANSION);
        app->expansion = NULL;
    }

    if(app->power) {
        furi_record_close(RECORD_POWER);
        app->power = NULL;
    }

    app->module_state = FuseRadioModuleStateInactive;
}

void fuse_radio_app_retry_session(FuseRadioApp* app) {
    furi_assert(app);
    fuse_radio_app_stop_session(app);
    fuse_radio_app_bootstrap_session(app, true);
}

bool fuse_radio_app_start_wifi_scan(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_app_reset_scan_results(app);
    app->current_request = FuseRadioRequestScan;
    app->wifi_state = FuseRadioWifiStateScanRequested;
    fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionScanning, "SCANNING");
    app->scan_dirty = true;

    if(app->module_state != FuseRadioModuleStateDetected) {
        app->wifi_state = FuseRadioWifiStateError;
        app->scan_results.has_error = true;
        strncpy(
            app->scan_results.error, "Board is not ready", sizeof(app->scan_results.error) - 1U);
        app->scan_results.error[sizeof(app->scan_results.error) - 1U] = '\0';
        return false;
    }

    if(!fuse_radio_app_send_wifi_scan(app)) {
        app->wifi_state = FuseRadioWifiStateError;
        app->scan_results.has_error = true;
        strncpy(
            app->scan_results.error, "UART write failed", sizeof(app->scan_results.error) - 1U);
        app->scan_results.error[sizeof(app->scan_results.error) - 1U] = '\0';
        app->scan_dirty = true;
        return false;
    }

    app->wifi_state = FuseRadioWifiStateScanning;
    app->scan_dirty = true;
    return true;
}

bool fuse_radio_app_request_led_status(FuseRadioApp* app) {
    furi_assert(app);

    app->current_request = FuseRadioRequestLedStatus;
    if(!fuse_radio_app_send_led_status(app)) {
        app->current_request = FuseRadioRequestNone;
        return false;
    }

    return true;
}

bool fuse_radio_app_set_led_manual(FuseRadioApp* app) {
    furi_assert(app);

    app->current_request = FuseRadioRequestLedSet;
    app->led_manual_override = true;
    if(!fuse_radio_app_send_led_set(app)) {
        app->current_request = FuseRadioRequestNone;
        return false;
    }

    return true;
}

bool fuse_radio_app_set_led_auto(FuseRadioApp* app) {
    furi_assert(app);

    app->current_request = FuseRadioRequestLedAuto;
    app->led_manual_override = false;
    if(!fuse_radio_app_send_led_auto(app)) {
        app->current_request = FuseRadioRequestNone;
        return false;
    }

    return true;
}

bool fuse_radio_app_start_ble_scan(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_app_reset_ble_scan_results(app);
    fuse_radio_app_clear_saved_ble_presence(app);
    app->current_request = FuseRadioRequestBleScan;
    app->ble_scan_results.active = true;
    app->ble_scan_results.scan_duration_ms = fuse_radio_app_ble_scan_duration_ms(app);
    app->ble_scan_results.progress_percent = 0U;
    app->ble_scan_started_at = furi_get_tick();

    if(app->module_state != FuseRadioModuleStateDetected) {
        app->current_request = FuseRadioRequestNone;
        app->ble_scan_results.active = false;
        app->ble_scan_results.complete = true;
        app->ble_scan_results.has_error = true;
        strncpy(
            app->ble_scan_results.error,
            "Board is not ready",
            sizeof(app->ble_scan_results.error) - 1U);
        app->ble_scan_results.error[sizeof(app->ble_scan_results.error) - 1U] = '\0';
        app->ble_dirty = true;
        return false;
    }

    if(!fuse_radio_app_send_ble_scan(app)) {
        app->current_request = FuseRadioRequestNone;
        app->ble_scan_results.active = false;
        app->ble_scan_results.complete = true;
        app->ble_scan_results.has_error = true;
        strncpy(
            app->ble_scan_results.error,
            "UART write failed",
            sizeof(app->ble_scan_results.error) - 1U);
        app->ble_scan_results.error[sizeof(app->ble_scan_results.error) - 1U] = '\0';
        app->ble_dirty = true;
        return false;
    }

    app->ble_dirty = true;
    return true;
}

bool fuse_radio_app_start_ble_distance(FuseRadioApp* app) {
    furi_assert(app);

    if(app->ble_selection.device.mac[0] == '\0') {
        return false;
    }

    fuse_radio_app_reset_ble_distance_state(app);
    app->current_request = FuseRadioRequestBleDistance;
    app->ble_distance_started_at = furi_get_tick();
    app->ble_distance_active = true;
    app->ble_dirty = true;

    if(app->module_state != FuseRadioModuleStateDetected) {
        app->current_request = FuseRadioRequestNone;
        app->ble_distance_active = false;
        app->ble_distance_has_error = true;
        fuse_radio_app_strlcpy(
            app->ble_distance_error,
            "Board is not ready",
            sizeof(app->ble_distance_error));
        app->ble_dirty = true;
        return false;
    }

    if(!fuse_radio_app_send_ble_distance_start(app)) {
        app->current_request = FuseRadioRequestNone;
        app->ble_distance_active = false;
        app->ble_distance_has_error = true;
        fuse_radio_app_strlcpy(
            app->ble_distance_error,
            "UART write failed",
            sizeof(app->ble_distance_error));
        app->ble_dirty = true;
        return false;
    }

    return true;
}

bool fuse_radio_app_stop_ble_distance(FuseRadioApp* app) {
    furi_assert(app);

    if(!app->ble_distance_active || app->ble_distance_stop_pending) {
        return false;
    }

    app->ble_distance_stop_pending = true;
    app->ble_dirty = true;

    if(!fuse_radio_app_send_ble_distance_stop(app)) {
        app->ble_distance_stop_pending = false;
        app->ble_distance_has_error = true;
        fuse_radio_app_strlcpy(
            app->ble_distance_error,
            "Stop command failed",
            sizeof(app->ble_distance_error));
        app->ble_dirty = true;
        return false;
    }

    return true;
}

bool fuse_radio_app_start_ble_gatt(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_app_reset_gatt_results(app);
    fuse_radio_app_strlcpy(
        app->gatt_results.mac,
        app->ble_selection.device.mac,
        sizeof(app->gatt_results.mac));
    app->current_request = FuseRadioRequestBleGatt;
    app->gatt_results.active = true;
    app->gatt_dirty = true;

    if(app->module_state != FuseRadioModuleStateDetected) {
        app->current_request = FuseRadioRequestNone;
        app->gatt_results.active = false;
        app->gatt_results.complete = true;
        app->gatt_results.has_error = true;
        strncpy(
            app->gatt_results.error,
            "Board is not ready",
            sizeof(app->gatt_results.error) - 1U);
        app->gatt_results.error[sizeof(app->gatt_results.error) - 1U] = '\0';
        app->gatt_dirty = true;
        return false;
    }

    if(!fuse_radio_app_send_ble_gatt(app)) {
        app->current_request = FuseRadioRequestNone;
        app->gatt_results.active = false;
        app->gatt_results.complete = true;
        app->gatt_results.has_error = true;
        strncpy(
            app->gatt_results.error,
            "UART write failed",
            sizeof(app->gatt_results.error) - 1U);
        app->gatt_results.error[sizeof(app->gatt_results.error) - 1U] = '\0';
        app->gatt_dirty = true;
        return false;
    }

    return true;
}

bool fuse_radio_app_select_ble_scan_device(FuseRadioApp* app) {
    furi_assert(app);

    if(app->ble_scan_results.count == 0U) {
        return false;
    }

    uint8_t selected_index = fuse_radio_ble_scan_view_get_selected_index(app->ble_scan_view);
    if(selected_index >= app->ble_scan_results.count) {
        return false;
    }

    memset(&app->ble_selection, 0, sizeof(app->ble_selection));
    app->ble_selection.device = app->ble_scan_results.devices[selected_index];
    app->ble_selection.seen_recently = true;
    app->ble_selection.is_saved =
        fuse_radio_app_find_saved_ble_device_index(app, app->ble_selection.device.mac) >= 0;
    return true;
}

bool fuse_radio_app_select_saved_ble_device(FuseRadioApp* app) {
    furi_assert(app);

    if(app->saved_ble_results.count == 0U) {
        return false;
    }

    uint8_t selected_index = fuse_radio_ble_scan_view_get_selected_index(app->ble_scan_view);
    if(selected_index >= app->saved_ble_results.count) {
        return false;
    }

    const FuseRadioSavedBleDevice* saved = &app->saved_ble_results.devices[selected_index];
    memset(&app->ble_selection, 0, sizeof(app->ble_selection));
    fuse_radio_app_strlcpy(app->ble_selection.device.mac, saved->mac, sizeof(app->ble_selection.device.mac));
    fuse_radio_app_strlcpy(
        app->ble_selection.device.addr_type,
        saved->addr_type,
        sizeof(app->ble_selection.device.addr_type));
    app->ble_selection.device.connectable = saved->connectable;
    app->ble_selection.device.has_name = saved->has_name;
    app->ble_selection.device.has_company = saved->has_company;
    app->ble_selection.device.has_appearance = saved->has_appearance;
    if(saved->has_name) {
        fuse_radio_app_strlcpy(
            app->ble_selection.device.name,
            saved->name,
            sizeof(app->ble_selection.device.name));
    }
    if(saved->has_company) {
        fuse_radio_app_strlcpy(
            app->ble_selection.device.company,
            saved->company,
            sizeof(app->ble_selection.device.company));
    }
    if(saved->has_appearance) {
        fuse_radio_app_strlcpy(
            app->ble_selection.device.appearance,
            saved->appearance,
            sizeof(app->ble_selection.device.appearance));
    }
    fuse_radio_app_strlcpy(
        app->ble_selection.device.classification,
        saved->classification,
        sizeof(app->ble_selection.device.classification));
    fuse_radio_app_strlcpy(
        app->ble_selection.device.proximity,
        saved->proximity,
        sizeof(app->ble_selection.device.proximity));
    app->ble_selection.device.rssi = saved->last_rssi;
    app->ble_selection.is_saved = true;
    app->ble_selection.seen_recently = saved->seen_recently;
    return true;
}

bool fuse_radio_app_save_selected_ble_device(FuseRadioApp* app) {
    furi_assert(app);

    const FuseRadioBleDevice* source = NULL;

    if(app->ble_selection.device.mac[0]) {
        source = &app->ble_selection.device;
    } else if(app->ble_scan_results.count > 0U) {
        uint8_t selected_index = fuse_radio_ble_scan_view_get_selected_index(app->ble_scan_view);
        if(selected_index >= app->ble_scan_results.count) {
            selected_index = 0U;
        }
        source = &app->ble_scan_results.devices[selected_index];
    }

    if(source == NULL) {
        return false;
    }

    int32_t saved_index = fuse_radio_app_find_saved_ble_device_index(app, source->mac);
    if(saved_index < 0) {
        if(app->saved_ble_results.count < FUSE_RADIO_MAX_SAVED_BLE_DEVICES) {
            saved_index = (int32_t)app->saved_ble_results.count++;
        } else {
            memmove(
                &app->saved_ble_results.devices[0],
                &app->saved_ble_results.devices[1],
                sizeof(app->saved_ble_results.devices[0]) *
                    (FUSE_RADIO_MAX_SAVED_BLE_DEVICES - 1U));
            saved_index = FUSE_RADIO_MAX_SAVED_BLE_DEVICES - 1;
        }
    }

    FuseRadioSavedBleDevice* saved = &app->saved_ble_results.devices[saved_index];
    memset(saved, 0, sizeof(*saved));
    fuse_radio_app_strlcpy(saved->mac, source->mac, sizeof(saved->mac));
    fuse_radio_app_strlcpy(saved->addr_type, source->addr_type, sizeof(saved->addr_type));
    saved->connectable = source->connectable;
    saved->has_name = source->has_name;
    saved->has_company = source->has_company;
    saved->has_appearance = source->has_appearance;
    if(source->has_name) {
        fuse_radio_app_strlcpy(saved->name, source->name, sizeof(saved->name));
    }
    if(source->has_company) {
        fuse_radio_app_strlcpy(saved->company, source->company, sizeof(saved->company));
    }
    if(source->has_appearance) {
        fuse_radio_app_strlcpy(saved->appearance, source->appearance, sizeof(saved->appearance));
    }
    fuse_radio_app_strlcpy(
        saved->classification, source->classification, sizeof(saved->classification));
    fuse_radio_app_strlcpy(saved->proximity, source->proximity, sizeof(saved->proximity));
    saved->last_rssi = source->rssi;
    saved->seen_recently = true;
    app->ble_selection.is_saved = true;
    app->ble_dirty = true;

    return fuse_radio_app_save_ble_devices(app);
}

bool fuse_radio_app_remove_selected_ble_device(FuseRadioApp* app) {
    furi_assert(app);

    if(!app->ble_selection.is_saved || !app->ble_selection.device.mac[0]) {
        return false;
    }

    const int32_t saved_index =
        fuse_radio_app_find_saved_ble_device_index(app, app->ble_selection.device.mac);
    if(saved_index < 0) {
        return false;
    }

    if((uint8_t)saved_index + 1U < app->saved_ble_results.count) {
        memmove(
            &app->saved_ble_results.devices[saved_index],
            &app->saved_ble_results.devices[saved_index + 1],
            sizeof(app->saved_ble_results.devices[0]) *
                (app->saved_ble_results.count - (uint8_t)saved_index - 1U));
    }

    app->saved_ble_results.count--;
    app->ble_selection.is_saved = false;
    app->ble_dirty = true;
    return fuse_radio_app_save_ble_devices(app);
}

void fuse_radio_app_refresh_status_widget(FuseRadioApp* app) {
    furi_assert(app);

    FuseRadioStartupState startup_state = FuseRadioStartupStateBooting;

    if(app->module_state == FuseRadioModuleStateError) {
        startup_state = FuseRadioStartupStateError;
    } else if(app->module_state == FuseRadioModuleStateDetected) {
        startup_state = FuseRadioStartupStateReady;
    }

    fuse_radio_startup_view_set_state(app->startup_view, startup_state);

    app->status_dirty = false;
}

void fuse_radio_app_refresh_wifi_info_widget(FuseRadioApp* app) {
    furi_assert(app);

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, "WiFi Status");
    widget_add_text_scroll_element(app->widget, 0, 15, 128, 38, app->wifi_info_text);
    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "Menu", fuse_radio_app_wifi_status_button_callback, app);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "Refresh",
        fuse_radio_app_wifi_status_button_callback,
        app);

    app->wifi_info_dirty = false;
}

void fuse_radio_app_refresh_mdns_widget(FuseRadioApp* app) {
    furi_assert(app);

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, "mDNS");
    widget_add_text_scroll_element(app->widget, 0, 15, 128, 38, app->mdns_info_text);
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Query", fuse_radio_app_wifi_mdns_button_callback, app);

    app->mdns_dirty = false;
}

void fuse_radio_app_refresh_http_widget(FuseRadioApp* app) {
    furi_assert(app);

    const bool http_active = app->current_request == FuseRadioRequestHttp;

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, app->http_title);
    widget_add_text_scroll_element(app->widget, 0, 15, 128, 38, app->http_info_text);
    if(!http_active) {
        widget_add_button_element(
            app->widget,
            GuiButtonTypeCenter,
            "Query",
            fuse_radio_app_wifi_http_button_callback,
            app);
    }

    app->http_dirty = false;
}

void fuse_radio_app_refresh_promiscuous_widget(FuseRadioApp* app) {
    furi_assert(app);

    const bool survey_active = app->current_request == FuseRadioRequestPromiscuousSurvey;

    if(app->promiscuous_watch_live_active) {
        fuse_radio_app_refresh_promiscuous_live_view(app);
    } else if(app->watch_summary.has_summary && app->watch_device_count > 0U) {
        fuse_radio_app_refresh_watch_result_view(app);
    } else {
        widget_reset(app->widget);
        widget_add_string_element(
            app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, "Promisc");
        widget_add_text_scroll_element(app->widget, 0, 15, 128, 38, app->promiscuous_info_text);
        widget_add_button_element(
            app->widget,
            GuiButtonTypeLeft,
            "Menu",
            fuse_radio_app_wifi_promiscuous_button_callback,
            app);
        if(!survey_active) {
            widget_add_button_element(
                app->widget,
                GuiButtonTypeCenter,
                "Repeat",
                fuse_radio_app_wifi_promiscuous_button_callback,
                app);
        }
    }

    app->promiscuous_dirty = false;
}

void fuse_radio_app_refresh_scan_view(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_scan_view_set_data(app->scan_view, &app->scan_results, app->wifi_state);
    app->scan_dirty = false;
}

void fuse_radio_app_refresh_sd_widget(FuseRadioApp* app) {
    furi_assert(app);

    const char* title = "SD Card";
    if(app->sd_action == FuseRadioSdActionExplore) {
        title = "SD Explore";
    } else if(app->sd_action == FuseRadioSdActionDetail) {
        title = "SD Detail";
    } else if(app->sd_action == FuseRadioSdActionFormat) {
        title = app->sd_confirm_format ? "SD Format?" : "SD Format";
    }

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, title);
    widget_add_text_scroll_element(app->widget, 0, 14, 128, 38, app->sd_info_text);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "Refresh",
        fuse_radio_app_sd_widget_button_callback,
        app);

    if(app->sd_action == FuseRadioSdActionDetail || app->sd_action == FuseRadioSdActionFormat ||
       app->sd_confirm_format) {
        widget_add_button_element(
            app->widget,
            GuiButtonTypeRight,
            app->sd_confirm_format ? "Confirm" : "Format",
            fuse_radio_app_sd_widget_button_callback,
            app);
    }

    app->sd_dirty = false;
}

bool fuse_radio_app_start_sd_action(FuseRadioApp* app, FuseRadioSdAction action) {
    furi_assert(app);

    app->sd_action = action;
    app->current_request = FuseRadioRequestNone;

    if(app->module_state != FuseRadioModuleStateDetected) {
        snprintf(app->sd_info_text, sizeof(app->sd_info_text), "Board is not ready.");
        app->sd_dirty = true;
        return false;
    }

    switch(action) {
    case FuseRadioSdActionExplore:
        if(app->sd_explore_path[0] == '\0') {
            snprintf(app->sd_explore_path, sizeof(app->sd_explore_path), "/");
        }
        snprintf(
            app->sd_info_text,
            sizeof(app->sd_info_text),
            "Path: %s\n\nLoading directory...",
            app->sd_explore_path[0] ? app->sd_explore_path : "/");
        app->sd_entry_count = 0U;
        app->sd_confirm_format = false;
        if(!fuse_radio_app_send_sd_list_command(app, app->sd_explore_path)) {
            snprintf(app->sd_info_text, sizeof(app->sd_info_text), "UART write failed.");
            app->sd_dirty = true;
            return false;
        }
        break;
    case FuseRadioSdActionDetail:
        app->sd_confirm_format = false;
        snprintf(
            app->sd_info_text,
            sizeof(app->sd_info_text),
            "Reading card status...");
        if(!fuse_radio_app_send_sd_info_command(app)) {
            snprintf(app->sd_info_text, sizeof(app->sd_info_text), "UART write failed.");
            app->sd_dirty = true;
            return false;
        }
        break;
    case FuseRadioSdActionFormat:
        snprintf(
            app->sd_info_text,
            sizeof(app->sd_info_text),
            "Formatting card...");
        app->sd_confirm_format = false;
        if(!fuse_radio_app_send_sd_format_command(app)) {
            snprintf(app->sd_info_text, sizeof(app->sd_info_text), "UART write failed.");
            app->sd_dirty = true;
            return false;
        }
        break;
    case FuseRadioSdActionNone:
    default:
        snprintf(app->sd_info_text, sizeof(app->sd_info_text), "No SD action selected.");
        break;
    }

    app->sd_dirty = true;
    return true;
}

static bool fuse_radio_app_scene_requires_connected(uint32_t scene) {
    return scene == FuseRadioSceneWifiConnectedMenu ||
           scene == FuseRadioSceneWifiDiscoverProgress ||
           scene == FuseRadioSceneWifiDiscoverResult ||
           scene == FuseRadioSceneWifiHttpResult ||
           scene == FuseRadioSceneWifiMdnsHost ||
           scene == FuseRadioSceneWifiMdnsResult;
}

static bool fuse_radio_app_scene_requires_promiscuous(uint32_t scene) {
    return scene == FuseRadioSceneWifiPromiscuousMenu ||
            scene == FuseRadioSceneWifiPromiscuousSurveyPreset ||
            scene == FuseRadioSceneWifiPromiscuousSurveyProgress ||
            scene == FuseRadioSceneWifiPromiscuousSurveyResult ||
           scene == FuseRadioSceneWifiPromiscuousWatchChannel ||
           scene == FuseRadioSceneWifiPromiscuousResult;
}

static bool fuse_radio_app_survey_active(const FuseRadioApp* app) {
    return app->current_request == FuseRadioRequestPromiscuousSurvey;
}

static bool fuse_radio_app_scene_mode_is_valid(FuseRadioApp* app, uint32_t scene) {
    if(fuse_radio_app_scene_requires_connected(scene)) {
        if(app->wifi_mode == FuseRadioWifiModeConnected) {
            return true;
        }

        if(app->current_request == FuseRadioRequestConnect ||
           app->current_request == FuseRadioRequestDiscover ||
              app->current_request == FuseRadioRequestHttp ||
           app->current_request == FuseRadioRequestMdns ||
           app->wifi_action == FuseRadioWifiActionConnecting ||
           app->wifi_action == FuseRadioWifiActionDiscovering ||
              app->wifi_action == FuseRadioWifiActionRequestingHttp ||
           app->wifi_action == FuseRadioWifiActionResolvingMdns) {
            return true;
        }

        return false;
    }

    if(fuse_radio_app_scene_requires_promiscuous(scene)) {
        if(app->wifi_mode == FuseRadioWifiModePromiscuous) {
            return true;
        }

        if(app->current_request == FuseRadioRequestPromiscuousEnter ||
           app->current_request == FuseRadioRequestPromiscuousSurvey ||
           app->current_request == FuseRadioRequestPromiscuousWatch ||
           app->wifi_action == FuseRadioWifiActionEnteringPromiscuous ||
           app->wifi_action == FuseRadioWifiActionSurveying ||
           app->wifi_action == FuseRadioWifiActionWatching) {
            return true;
        }

        return false;
    }

    return true;
}

static void fuse_radio_app_handle_scene_mode_guard(FuseRadioApp* app, uint32_t scene) {
    const uint32_t now = furi_get_tick();

    if(app->module_state != FuseRadioModuleStateDetected) {
        return;
    }

    if(!fuse_radio_app_scene_requires_connected(scene) &&
       !fuse_radio_app_scene_requires_promiscuous(scene)) {
        return;
    }

    if(app->current_request == FuseRadioRequestNone &&
       (now - app->last_mode_guard_poll_at) >= FUSE_RADIO_MODE_GUARD_POLL_MS) {
        if(fuse_radio_app_send_line(app, "WIFI STATUS\n")) {
            app->last_mode_guard_poll_at = now;
            app->current_request = FuseRadioRequestStatus;
        }
    }

    if(!fuse_radio_app_scene_mode_is_valid(app, scene)) {
        app->connect_flow_active = false;
        app->promiscuous_preset = FuseRadioPromiscuousPresetNone;
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, FuseRadioSceneWifiMenu);
    }
}

static bool fuse_radio_app_discover_stream_active(const FuseRadioApp* app) {
    return app->current_request == FuseRadioRequestDiscover;
}

static bool fuse_radio_app_promiscuous_stream_active(const FuseRadioApp* app) {
    return app->current_request == FuseRadioRequestPromiscuousSurvey;
}

void fuse_radio_app_handle_tick(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_app_process_rx(app);
    fuse_radio_app_update_ble_scan_progress(app);

    if(app->module_state == FuseRadioModuleStatePowering) {
        const uint32_t now = furi_get_tick();

        if(now >= app->next_startup_action_at) {
            fuse_radio_app_bootstrap_session(app, false);
        }
    }

    if(app->module_state == FuseRadioModuleStateWaitingReady) {
        const uint32_t now = furi_get_tick();

        if((now - app->detect_started_at) >= FUSE_RADIO_DETECT_TIMEOUT_MS) {
            if(app->startup_link_retry_count < FUSE_RADIO_STARTUP_MAX_LINK_RETRIES) {
                app->startup_link_retry_count++;
                fuse_radio_app_stop_session(app);
                fuse_radio_app_bootstrap_session(app, false);
                {
                    char detail[64];

                    snprintf(
                        detail,
                        sizeof(detail),
                        "No reply. Restarting link %u/%u.",
                        (unsigned)app->startup_link_retry_count,
                        (unsigned)FUSE_RADIO_STARTUP_MAX_LINK_RETRIES);
                    fuse_radio_app_set_status(app, detail);
                }
            } else {
                fuse_radio_app_set_error(app, "No UART response\nfrom the ESP board");
            }
        } else if(
            now >= app->next_startup_action_at &&
            (app->last_ping_at == 0U ||
             ((now - app->last_ping_at) >= FUSE_RADIO_PING_INTERVAL_MS))) {
            if(fuse_radio_app_send_ping(app)) {
                app->last_ping_at = now;
                if(app->startup_link_retry_count == 0U) {
                    fuse_radio_app_set_status(app, "Sent PING. Waiting for PONG.");
                } else {
                    char detail[64];
                    snprintf(
                        detail,
                        sizeof(detail),
                        "Retry %u/%u: sent PING.",
                        (unsigned)app->startup_link_retry_count,
                        (unsigned)FUSE_RADIO_STARTUP_MAX_LINK_RETRIES);
                    fuse_radio_app_set_status(app, detail);
                }
            }
        }
    }

    if((app->module_state == FuseRadioModuleStateDetected) && !app->module_detect_event_sent) {
        app->module_detect_event_sent = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventModuleDetected);
    }

    const uint32_t scene = scene_manager_get_current_scene(app->scene_manager);
    fuse_radio_app_handle_scene_mode_guard(app, scene);

    if(scene == FuseRadioSceneStatus) {
        if(app->status_dirty) {
            fuse_radio_app_refresh_status_widget(app);
        } else {
            fuse_radio_startup_view_advance_animation(app->startup_view);
        }
    }
    if(app->ble_dirty && scene == FuseRadioSceneBleScan) {
        fuse_radio_app_refresh_ble_scan_view(app);
    } else if(scene == FuseRadioSceneBleScan && app->ble_scan_results.active) {
        fuse_radio_ble_scan_view_advance_animation(app->ble_scan_view);
    }
    if(app->ble_dirty && scene == FuseRadioSceneBleSavedDevices) {
        fuse_radio_app_refresh_saved_ble_view(app);
    }
    if(app->ble_dirty && scene == FuseRadioSceneBleDistance) {
        fuse_radio_app_refresh_ble_distance_widget(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
    }
    if(app->scan_dirty &&
       (scene == FuseRadioSceneWifiScan || scene == FuseRadioSceneWifiConnectSsid)) {
        fuse_radio_app_refresh_scan_view(app);
    } else if(
        (scene == FuseRadioSceneWifiScan || scene == FuseRadioSceneWifiConnectSsid) &&
        app->wifi_state == FuseRadioWifiStateScanning) {
        fuse_radio_scan_view_advance_animation(app->scan_view);
    }
    if(app->wifi_info_dirty && scene == FuseRadioSceneWifiStatus) {
        fuse_radio_app_refresh_wifi_info_widget(app);
    }
    if(scene == FuseRadioSceneWifiDiscoverProgress) {
        if(app->discover_dirty) {
            fuse_radio_app_refresh_discover_progress_view(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewDiscoverProgress);
        } else if(fuse_radio_app_discover_stream_active(app)) {
            fuse_radio_discover_progress_view_advance_animation(app->discover_progress_view);
        }
    }
    if(app->discover_dirty && scene == FuseRadioSceneWifiDiscoverResult &&
       !fuse_radio_app_discover_stream_active(app)) {
        fuse_radio_app_refresh_discover_result_view(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewDiscoverResult);
    }
    if(app->mdns_dirty && scene == FuseRadioSceneWifiMdnsResult) {
        fuse_radio_app_refresh_mdns_widget(app);
    }
    if(app->http_dirty && scene == FuseRadioSceneWifiHttpResult) {
        fuse_radio_app_refresh_http_widget(app);
    }
    if(scene == FuseRadioSceneWifiPromiscuousSurveyProgress) {
        if(app->promiscuous_dirty) {
            fuse_radio_app_refresh_survey_progress_view(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSurveyProgress);
        } else if(fuse_radio_app_survey_active(app)) {
            fuse_radio_survey_progress_view_advance_animation(app->survey_progress_view);
        }
    }
    if(app->promiscuous_dirty && scene == FuseRadioSceneWifiPromiscuousSurveyResult) {
        fuse_radio_app_refresh_survey_result_view(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSurveyResult);
    }
    if(app->promiscuous_dirty && scene == FuseRadioSceneWifiPromiscuousResult &&
       !fuse_radio_app_promiscuous_stream_active(app)) {
        fuse_radio_app_refresh_promiscuous_widget(app);
        view_dispatcher_switch_to_view(
            app->view_dispatcher,
            app->promiscuous_watch_live_active ?
                FuseRadioViewWatchLive :
                (app->watch_summary.has_summary && app->watch_device_count > 0U ?
                     FuseRadioViewWatchResult :
                     FuseRadioViewWidget));
    }
    if(app->sd_dirty && scene == FuseRadioSceneSdResult) {
        fuse_radio_app_refresh_sd_widget(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
    }
    if(app->sd_dirty && scene == FuseRadioSceneSdExplore) {
        fuse_radio_app_refresh_sd_explore_submenu(app);
    }
}

FuseRadioApp* fuse_radio_app_alloc(void) {
    FuseRadioApp* app = malloc(sizeof(FuseRadioApp));
    memset(app, 0, sizeof(FuseRadioApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&fuse_radio_scene_handlers, app);
    app->widget = widget_alloc();
    app->submenu = submenu_alloc();
    app->startup_view = fuse_radio_startup_view_alloc();
    app->scan_view = fuse_radio_scan_view_alloc();
    app->ble_scan_view = fuse_radio_ble_scan_view_alloc();
    app->text_input = text_input_alloc();
    app->channel_picker_view = fuse_radio_channel_picker_view_alloc();
    app->value_picker_view = fuse_radio_value_picker_view_alloc();
    app->discover_progress_view = fuse_radio_discover_progress_view_alloc();
    app->discover_result_view = fuse_radio_discover_result_view_alloc();
    app->survey_preset_view = fuse_radio_survey_preset_view_alloc();
    app->survey_progress_view = fuse_radio_survey_progress_view_alloc();
    app->survey_result_view = fuse_radio_survey_result_view_alloc();
    app->watch_live_view = fuse_radio_watch_live_view_alloc();
    app->watch_result_view = fuse_radio_watch_result_view_alloc();
    app->variable_item_list = variable_item_list_alloc();
    app->rx_stream = furi_stream_buffer_alloc(FUSE_RADIO_RX_STREAM_SIZE, 1U);
    app->power = furi_record_open(RECORD_POWER);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->credentials_format = flipper_format_file_alloc(app->storage);

    app->ble_scan_mode = FuseRadioBleScanModeNormal;
    fuse_radio_app_reset_wifi_status(app);
    fuse_radio_app_reset_led_state(app);
    fuse_radio_app_reset_ble_distance_state(app);
    app->promiscuous_watch_channel = 1U;
    fuse_radio_app_set_status(app, "Preparing module session.");
    fuse_radio_app_load_credentials(app);
    fuse_radio_app_load_ble_devices(app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, fuse_radio_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, fuse_radio_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, fuse_radio_app_tick_event_callback, 100U);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewStartup,
        fuse_radio_startup_view_get_view(app->startup_view));
    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewWidget, widget_get_view(app->widget));
    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewScan, fuse_radio_scan_view_get_view(app->scan_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewBleScan,
        fuse_radio_ble_scan_view_get_view(app->ble_scan_view));
    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewTextInput, text_input_get_view(app->text_input));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewChannelPicker,
        fuse_radio_channel_picker_view_get_view(app->channel_picker_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewValuePicker,
        fuse_radio_value_picker_view_get_view(app->value_picker_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewDiscoverProgress,
        fuse_radio_discover_progress_view_get_view(app->discover_progress_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewDiscoverResult,
        fuse_radio_discover_result_view_get_view(app->discover_result_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewSurveyPreset,
        fuse_radio_survey_preset_view_get_view(app->survey_preset_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewSurveyProgress,
        fuse_radio_survey_progress_view_get_view(app->survey_progress_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewSurveyResult,
        fuse_radio_survey_result_view_get_view(app->survey_result_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewWatchLive,
        fuse_radio_watch_live_view_get_view(app->watch_live_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewWatchResult,
        fuse_radio_watch_result_view_get_view(app->watch_result_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));

    fuse_radio_scan_view_set_callback(app->scan_view, fuse_radio_app_scan_view_callback, app);
    fuse_radio_ble_scan_view_set_callback(
        app->ble_scan_view, fuse_radio_app_ble_scan_view_callback, app);
    fuse_radio_channel_picker_view_set_callback(
        app->channel_picker_view, fuse_radio_app_watch_channel_callback, app);
    fuse_radio_value_picker_view_set_callback(
        app->value_picker_view, fuse_radio_app_led_value_callback, app);
    fuse_radio_startup_view_set_callback(
        app->startup_view, fuse_radio_app_startup_view_callback, app);
    fuse_radio_discover_result_view_set_callback(
        app->discover_result_view, fuse_radio_app_discover_result_view_callback, app);
    fuse_radio_survey_preset_view_set_callback(
        app->survey_preset_view, fuse_radio_app_survey_preset_view_callback, app);
    fuse_radio_survey_result_view_set_callback(
        app->survey_result_view, fuse_radio_app_survey_result_view_callback, app);
    fuse_radio_watch_live_view_set_callback(
        app->watch_live_view, fuse_radio_app_watch_live_view_callback, app);
    fuse_radio_watch_result_view_set_callback(
        app->watch_result_view, fuse_radio_app_watch_result_view_callback, app);

    fuse_radio_app_start_session(app);
    scene_manager_next_scene(app->scene_manager, FuseRadioSceneStatus);

    return app;
}

void fuse_radio_app_run(FuseRadioApp* app) {
    furi_assert(app);
    view_dispatcher_run(app->view_dispatcher);
}

void fuse_radio_app_free(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_app_stop_session(app);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewStartup);
    fuse_radio_startup_view_free(app->startup_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewScan);
    fuse_radio_scan_view_free(app->scan_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewBleScan);
    fuse_radio_ble_scan_view_free(app->ble_scan_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewTextInput);
    text_input_free(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewChannelPicker);
    fuse_radio_channel_picker_view_free(app->channel_picker_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewValuePicker);
    fuse_radio_value_picker_view_free(app->value_picker_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewDiscoverProgress);
    fuse_radio_discover_progress_view_free(app->discover_progress_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewDiscoverResult);
    fuse_radio_discover_result_view_free(app->discover_result_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewSurveyPreset);
    fuse_radio_survey_preset_view_free(app->survey_preset_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewSurveyProgress);
    fuse_radio_survey_progress_view_free(app->survey_progress_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewSurveyResult);
    fuse_radio_survey_result_view_free(app->survey_result_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewWatchLive);
    fuse_radio_watch_live_view_free(app->watch_live_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewWatchResult);
    fuse_radio_watch_result_view_free(app->watch_result_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewVariableItemList);
    variable_item_list_free(app->variable_item_list);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewSubmenu);
    submenu_free(app->submenu);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewWidget);
    widget_free(app->widget);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_stream_buffer_free(app->rx_stream);

    if(app->power) {
        furi_record_close(RECORD_POWER);
        app->power = NULL;
    }

    if(app->credentials_format) {
        flipper_format_free(app->credentials_format);
        app->credentials_format = NULL;
    }

    if(app->storage) {
        furi_record_close(RECORD_STORAGE);
        app->storage = NULL;
    }

    furi_record_close(RECORD_GUI);
    free(app);
}
