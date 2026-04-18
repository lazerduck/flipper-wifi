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

static void fuse_radio_app_watch_channel_callback(uint8_t channel, void* context) {
    FuseRadioApp* app = context;
    app->promiscuous_watch_channel = channel;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, FuseRadioCustomEventWifiWatchChannelStart);
}
static void fuse_radio_app_watch_live_view_callback(void* context) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventWifiPromiscuousStop);
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

static void
    fuse_radio_app_status_button_callback(GuiButtonType button, InputType type, void* context) {
    FuseRadioApp* app = context;

    if(button == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventRetryDetection);
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

static void fuse_radio_app_wifi_discover_button_callback(
    GuiButtonType button,
    InputType type,
    void* context) {
    FuseRadioApp* app = context;

    if(button == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiDiscoverRefresh);
    }
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

static void fuse_radio_app_reset_mdns_results(FuseRadioApp* app) {
    app->mdns_info_text[0] = '\0';
    app->mdns_count = 0U;
    app->mdns_truncated_count = 0U;
    app->mdns_dirty = true;
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
    app->discover_info_text[0] = '\0';
    app->discover_scanned_count = 0U;
    app->discover_found_count = 0U;
    app->discover_duration_ms = 0U;
    app->discover_dirty = true;
}

static void fuse_radio_app_set_wifi_info_text(FuseRadioApp* app, const char* text) {
    strncpy(app->wifi_info_text, text, sizeof(app->wifi_info_text) - 1U);
    app->wifi_info_text[sizeof(app->wifi_info_text) - 1U] = '\0';
    app->wifi_info_dirty = true;
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

static void fuse_radio_app_append_discover_text(FuseRadioApp* app, const char* fmt, ...) {
    const size_t used = strlen(app->discover_info_text);
    if(used >= sizeof(app->discover_info_text) - 1U) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(app->discover_info_text + used, sizeof(app->discover_info_text) - used, fmt, args);
    va_end(args);
    app->discover_dirty = true;
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
    fuse_radio_app_reset_discover_results(app);
    fuse_radio_app_reset_mdns_results(app);
    fuse_radio_app_reset_promiscuous_results(app);
    fuse_radio_app_reset_wifi_status(app);
    app->current_request = FuseRadioRequestNone;
    app->text_input_mode = FuseRadioTextInputNone;
    app->promiscuous_preset = FuseRadioPromiscuousPresetNone;
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

static bool fuse_radio_app_send_wifi_scan(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "WIFI SCAN\n");
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
        fuse_radio_app_append_discover_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestDiscover;
    fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionDiscovering, "DISCOVERING");
    fuse_radio_app_reset_discover_results(app);
    fuse_radio_app_append_discover_text(app, "Probing local subnet...\n");

    if(!fuse_radio_app_send_wifi_discover_command(app)) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_reset_discover_results(app);
        fuse_radio_app_append_discover_text(app, "UART write failed while starting discovery.");
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

static void fuse_radio_app_parse_discover_network_line(FuseRadioApp* app, const char* line) {
    const char* subnet = strstr(line, "subnet=");
    const char* self = strstr(line, " self=");

    if(!subnet || !self) {
        fuse_radio_app_append_discover_text(app, "%s\n", line);
        return;
    }

    subnet += 7;
    self += 6;

    char subnet_text[32] = {0};
    char self_text[32] = {0};
    size_t subnet_len = (size_t)(strstr(subnet, " self=") - subnet);
    size_t self_len = strcspn(self, " ");

    if(subnet_len >= sizeof(subnet_text)) subnet_len = sizeof(subnet_text) - 1U;
    if(self_len >= sizeof(self_text)) self_len = sizeof(self_text) - 1U;

    memcpy(subnet_text, subnet, subnet_len);
    memcpy(self_text, self, self_len);
    fuse_radio_app_append_discover_text(app, "Subnet: %s\nSelf: %s\n\n", subnet_text, self_text);
}

static void fuse_radio_app_parse_discover_found_line(FuseRadioApp* app, const char* line) {
    const char* ip = strstr(line, "ip=");
    const char* host = strstr(line, " host=");
    const char* rtt = strstr(line, " rtt_ms=");

    if(!ip || !host || !rtt) {
        fuse_radio_app_append_discover_text(app, "%s\n", line);
        return;
    }

    ip += 3;
    host += 6;
    rtt += 8;

    char ip_text[32] = {0};
    char host_text[64] = {0};
    char rtt_text[16] = {0};
    size_t ip_len = (size_t)(strstr(ip, " host=") - ip);
    size_t host_len = (size_t)(strstr(host, " rtt_ms=") - host);
    size_t rtt_len = strcspn(rtt, " ");

    if(ip_len >= sizeof(ip_text)) ip_len = sizeof(ip_text) - 1U;
    if(host_len >= sizeof(host_text)) host_len = sizeof(host_text) - 1U;
    if(rtt_len >= sizeof(rtt_text)) rtt_len = sizeof(rtt_text) - 1U;

    memcpy(ip_text, ip, ip_len);
    memcpy(host_text, host, host_len);
    memcpy(rtt_text, rtt, rtt_len);

    fuse_radio_app_append_discover_text(
        app,
        "%s\n%s  %sms\n\n",
        ip_text,
        strcmp(host_text, "-") == 0 ? "host:-" : host_text,
        rtt_text);
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
    } else if(strcmp(app->wifi_status_action, "RESOLVING_MDNS") == 0) {
        app->wifi_action = FuseRadioWifiActionResolvingMdns;
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

    if(app->current_request == FuseRadioRequestDiscover) {
        fuse_radio_app_reset_discover_results(app);
        fuse_radio_app_append_discover_text(app, "%s", line);
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        app->current_request = FuseRadioRequestNone;
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

    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_error(app, line);
    }
}

static void fuse_radio_app_handle_line(FuseRadioApp* app, const char* line) {
    if(strcmp(line, "READY") == 0 || strcmp(line, "PONG") == 0) {
        fuse_radio_app_mark_detected(app);
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
    } else if(strncmp(line, "MDNS_COUNT ", 11) == 0) {
        app->mdns_count = (uint8_t)strtoul(line + 11, NULL, 10);
        fuse_radio_app_reset_mdns_results(app);
        fuse_radio_app_append_mdns_text(app, "Results: %u\n\n", (unsigned)app->mdns_count);
    } else if(strncmp(line, "DISCOVER_NETWORK ", 17) == 0) {
        fuse_radio_app_reset_discover_results(app);
        fuse_radio_app_parse_discover_network_line(app, line);
    } else if(strncmp(line, "DISCOVER_FOUND ", 15) == 0) {
        fuse_radio_app_parse_discover_found_line(app, line);
    } else if(strncmp(line, "DISCOVER_DONE ", 14) == 0) {
        const char* scanned = strstr(line, "scanned=");
        const char* found = strstr(line, " found=");
        const char* duration = strstr(line, " duration_ms=");

        if(scanned) {
            app->discover_scanned_count = (uint16_t)strtoul(scanned + 8, NULL, 10);
        }
        if(found) {
            app->discover_found_count = (uint16_t)strtoul(found + 7, NULL, 10);
        }
        if(duration) {
            app->discover_duration_ms = (uint32_t)strtoul(duration + 13, NULL, 10);
        }

        fuse_radio_app_append_discover_text(
            app,
            "Summary: %u scanned, %u found\n%lu ms",
            (unsigned)app->discover_scanned_count,
            (unsigned)app->discover_found_count,
            (unsigned long)app->discover_duration_ms);
        fuse_radio_app_set_wifi_action(app, FuseRadioWifiActionNone, "NONE");
        app->current_request = FuseRadioRequestNone;
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

void fuse_radio_app_refresh_status_widget(FuseRadioApp* app) {
    furi_assert(app);

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, "Fuse Radio");

    const char* headline = "Starting";
    const char* detail = app->status_detail;

    if(app->module_state == FuseRadioModuleStatePowering) {
        headline = "Powering ESP";
    } else if(app->module_state == FuseRadioModuleStateWaitingReady) {
        headline = "Checking Link";
    } else if(app->module_state == FuseRadioModuleStateDetected) {
        headline = "Board Ready";
    } else if(app->module_state == FuseRadioModuleStateError) {
        headline = "No Module";
        detail = app->last_error;
    }

    widget_add_string_multiline_element(
        app->widget, 64, 28, AlignCenter, AlignCenter, FontSecondary, headline);
    widget_add_string_multiline_element(
        app->widget, 64, 44, AlignCenter, AlignCenter, FontSecondary, detail);

    if(app->module_state == FuseRadioModuleStateError) {
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, "Retry", fuse_radio_app_status_button_callback, app);
    }

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

void fuse_radio_app_refresh_discover_widget(FuseRadioApp* app) {
    furi_assert(app);

    const bool discover_active = app->current_request == FuseRadioRequestDiscover;

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, "Discover");
    widget_add_text_scroll_element(app->widget, 0, 15, 128, 38, app->discover_info_text);
    if(!discover_active) {
        widget_add_button_element(
            app->widget,
            GuiButtonTypeCenter,
            "Probe",
            fuse_radio_app_wifi_discover_button_callback,
            app);
    }

    app->discover_dirty = false;
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

static bool fuse_radio_app_scene_requires_connected(uint32_t scene) {
    return scene == FuseRadioSceneWifiConnectedMenu ||
           scene == FuseRadioSceneWifiDiscoverResult ||
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
           app->current_request == FuseRadioRequestMdns ||
           app->wifi_action == FuseRadioWifiActionConnecting ||
           app->wifi_action == FuseRadioWifiActionDiscovering ||
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

    if(app->status_dirty && scene == FuseRadioSceneStatus) {
        fuse_radio_app_refresh_status_widget(app);
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
    if(app->discover_dirty && scene == FuseRadioSceneWifiDiscoverResult &&
       !fuse_radio_app_discover_stream_active(app)) {
        fuse_radio_app_refresh_discover_widget(app);
    }
    if(app->mdns_dirty && scene == FuseRadioSceneWifiMdnsResult) {
        fuse_radio_app_refresh_mdns_widget(app);
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
}

FuseRadioApp* fuse_radio_app_alloc(void) {
    FuseRadioApp* app = malloc(sizeof(FuseRadioApp));
    memset(app, 0, sizeof(FuseRadioApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&fuse_radio_scene_handlers, app);
    app->widget = widget_alloc();
    app->submenu = submenu_alloc();
    app->scan_view = fuse_radio_scan_view_alloc();
    app->text_input = text_input_alloc();
    app->channel_picker_view = fuse_radio_channel_picker_view_alloc();
    app->survey_preset_view = fuse_radio_survey_preset_view_alloc();
    app->survey_progress_view = fuse_radio_survey_progress_view_alloc();
    app->survey_result_view = fuse_radio_survey_result_view_alloc();
    app->watch_live_view = fuse_radio_watch_live_view_alloc();
    app->watch_result_view = fuse_radio_watch_result_view_alloc();
    app->rx_stream = furi_stream_buffer_alloc(FUSE_RADIO_RX_STREAM_SIZE, 1U);
    app->power = furi_record_open(RECORD_POWER);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->credentials_format = flipper_format_file_alloc(app->storage);

    fuse_radio_app_reset_wifi_status(app);
    app->promiscuous_watch_channel = 1U;
    fuse_radio_app_set_status(app, "Preparing module session.");
    fuse_radio_app_load_credentials(app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, fuse_radio_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, fuse_radio_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, fuse_radio_app_tick_event_callback, 100U);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewWidget, widget_get_view(app->widget));
    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewScan, fuse_radio_scan_view_get_view(app->scan_view));
    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewTextInput, text_input_get_view(app->text_input));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FuseRadioViewChannelPicker,
        fuse_radio_channel_picker_view_get_view(app->channel_picker_view));
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

    fuse_radio_scan_view_set_callback(app->scan_view, fuse_radio_app_scan_view_callback, app);
    fuse_radio_channel_picker_view_set_callback(
        app->channel_picker_view, fuse_radio_app_watch_channel_callback, app);
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

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewScan);
    fuse_radio_scan_view_free(app->scan_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewTextInput);
    text_input_free(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewChannelPicker);
    fuse_radio_channel_picker_view_free(app->channel_picker_view);

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
