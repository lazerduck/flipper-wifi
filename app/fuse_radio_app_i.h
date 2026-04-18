#pragma once

#include "fuse_radio.h"
#include "fuse_radio_channel_picker_view.h"
#include "fuse_radio_survey_preset_view.h"
#include "fuse_radio_survey_progress_view.h"
#include "fuse_radio_survey_result_view.h"
#include "fuse_radio_watch_result_view.h"
#include "fuse_radio_watch_live_view.h"
#include "scenes/fuse_radio_scene.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <gui/view_dispatcher.h>

#include <expansion/expansion.h>
#include <flipper_format/flipper_format.h>
#include <power/power_service/power.h>
#include <furi_hal_power.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <storage/storage.h>

#define FUSE_RADIO_UART_BAUD_RATE     115200U
#define FUSE_RADIO_MAX_LINE_LENGTH    320U
#define FUSE_RADIO_STATUS_DETAIL_SIZE 64U
#define FUSE_RADIO_STATUS_ERROR_SIZE  64U
#define FUSE_RADIO_PLACEHOLDER_SIZE   64U
#define FUSE_RADIO_MAX_PASSWORD_LEN   64U
#define FUSE_RADIO_MAX_HOST_LEN       63U
#define FUSE_RADIO_MAX_SAVED_NETWORKS 12U
#define FUSE_RADIO_WIFI_INFO_SIZE     256U
#define FUSE_RADIO_HTTP_INFO_SIZE     768U
#define FUSE_RADIO_MDNS_INFO_SIZE     512U
#define FUSE_RADIO_DISCOVER_INFO_SIZE 1024U
#define FUSE_RADIO_PROMISCUOUS_INFO_SIZE 1024U
#define FUSE_RADIO_PROMISCUOUS_LIVE_SIZE 192U
#define FUSE_RADIO_DETECT_TIMEOUT_MS  5000U
#define FUSE_RADIO_PING_INTERVAL_MS   750U
#define FUSE_RADIO_MODE_GUARD_POLL_MS 1000U
#define FUSE_RADIO_STARTUP_SETTLE_MS  400U
#define FUSE_RADIO_STARTUP_RETRY_DELAY_MS 300U
#define FUSE_RADIO_STARTUP_MAX_LINK_RETRIES 2U
#define FUSE_RADIO_STARTUP_MAX_POWER_RETRIES 3U

typedef enum {
    FuseRadioViewWidget,
    FuseRadioViewSubmenu,
    FuseRadioViewScan,
    FuseRadioViewTextInput,
    FuseRadioViewChannelPicker,
    FuseRadioViewSurveyPreset,
    FuseRadioViewSurveyProgress,
    FuseRadioViewSurveyResult,
    FuseRadioViewWatchLive,
    FuseRadioViewWatchResult,
} FuseRadioView;

typedef enum {
    FuseRadioModuleStateInactive,
    FuseRadioModuleStatePowering,
    FuseRadioModuleStateWaitingReady,
    FuseRadioModuleStateDetected,
    FuseRadioModuleStateError,
} FuseRadioModuleState;

typedef enum {
    FuseRadioRequestNone,
    FuseRadioRequestScan,
    FuseRadioRequestStatus,
    FuseRadioRequestConnect,
    FuseRadioRequestDisconnect,
    FuseRadioRequestDiscover,
    FuseRadioRequestHttp,
    FuseRadioRequestMdns,
    FuseRadioRequestPromiscuousEnter,
    FuseRadioRequestPromiscuousExit,
    FuseRadioRequestPromiscuousSurvey,
    FuseRadioRequestPromiscuousWatch,
} FuseRadioRequest;

typedef enum {
    FuseRadioTextInputNone,
    FuseRadioTextInputConnectSsid,
    FuseRadioTextInputConnectPassword,
    FuseRadioTextInputMdnsHost,
} FuseRadioTextInputMode;

typedef enum {
    FuseRadioCustomEventModuleDetected = 0x100,
    FuseRadioCustomEventRetryDetection,
    FuseRadioCustomEventScanRescan,
    FuseRadioCustomEventScanSelect,
    FuseRadioCustomEventConnectSsidDone,
    FuseRadioCustomEventConnectPasswordDone,
    FuseRadioCustomEventMdnsHostDone,
    FuseRadioCustomEventWifiDiscoverRefresh,
    FuseRadioCustomEventWifiStatusRefresh,
    FuseRadioCustomEventWifiStatusMenu,
    FuseRadioCustomEventWifiHttpRefresh,
    FuseRadioCustomEventWifiMdnsRefresh,
    FuseRadioCustomEventWifiConnectedReady,
    FuseRadioCustomEventWifiPromiscuousRepeat,
    FuseRadioCustomEventWifiPromiscuousMenu,
    FuseRadioCustomEventWifiPromiscuousStop,
    FuseRadioCustomEventWifiWatchChannelStart,
    FuseRadioCustomEventWifiSurveyPresetStart,
    FuseRadioCustomEventWifiSurveyDone,
    FuseRadioCustomEventWifiSurveyFailed,
} FuseRadioCustomEvent;

typedef enum {
    FuseRadioHttpPresetNone,
    FuseRadioHttpPresetPublicIp,
    FuseRadioHttpPresetTime,
    FuseRadioHttpPresetLocation,
} FuseRadioHttpPreset;

typedef enum {
    FuseRadioPromiscuousPresetNone,
    FuseRadioPromiscuousPresetEnterChannel1,
    FuseRadioPromiscuousPresetExit,
    FuseRadioPromiscuousPresetSurvey11611,
    FuseRadioPromiscuousPresetSurvey111,
    FuseRadioPromiscuousPresetSurvey14814,
    FuseRadioPromiscuousPresetSurvey1357911,
    FuseRadioPromiscuousPresetWatchChannel,
} FuseRadioPromiscuousPreset;

typedef struct {
    char ssid[FUSE_RADIO_MAX_SSID_LENGTH + 1U];
    char password[FUSE_RADIO_MAX_PASSWORD_LEN + 1U];
} FuseRadioSavedCredential;

#include "fuse_radio_scan_view.h"

struct FuseRadioApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    Widget* widget;
    Submenu* submenu;
    FuseRadioScanView* scan_view;
    TextInput* text_input;
    FuseRadioChannelPickerView* channel_picker_view;
    FuseRadioSurveyPresetView* survey_preset_view;
    FuseRadioSurveyProgressView* survey_progress_view;
    FuseRadioSurveyResultView* survey_result_view;
    FuseRadioWatchLiveView* watch_live_view;
    FuseRadioWatchResultView* watch_result_view;

    Expansion* expansion;
    Power* power;
    Storage* storage;
    FuriHalSerialHandle* serial_handle;
    FuriStreamBuffer* rx_stream;
    FlipperFormat* credentials_format;

    FuseRadioModuleState module_state;
    FuseRadioWifiState wifi_state;
    FuseRadioWifiMode wifi_mode;
    FuseRadioWifiAction wifi_action;
    FuseRadioRequest current_request;
    FuseRadioTextInputMode text_input_mode;
    FuseRadioPromiscuousPreset promiscuous_preset;
    FuseRadioHttpPreset http_preset;
    FuseRadioScanResults scan_results;
    FuseRadioSurveyResults survey_results;
    FuseRadioWatchSummary watch_summary;
    FuseRadioWatchDevice watch_devices[FUSE_RADIO_MAX_WATCH_DEVICES];

    char line_buffer[FUSE_RADIO_MAX_LINE_LENGTH + 1U];
    size_t line_length;
    bool line_overflow;

    char status_detail[FUSE_RADIO_STATUS_DETAIL_SIZE];
    char last_error[FUSE_RADIO_STATUS_ERROR_SIZE];
    char placeholder_title[24];
    char placeholder_message[FUSE_RADIO_PLACEHOLDER_SIZE];
    char connect_ssid[FUSE_RADIO_MAX_SSID_LENGTH + 1U];
    char connect_password[FUSE_RADIO_MAX_PASSWORD_LEN + 1U];
    char skip_auto_password_ssid[FUSE_RADIO_MAX_SSID_LENGTH + 1U];
    char mdns_host[FUSE_RADIO_MAX_HOST_LEN + 1U];
    char wifi_status_state[16];
    char wifi_status_mode[16];
    char wifi_status_action[24];
    char wifi_status_ssid[FUSE_RADIO_MAX_SSID_LENGTH + 1U];
    char wifi_info_text[FUSE_RADIO_WIFI_INFO_SIZE];
    char http_title[24];
    char http_info_text[FUSE_RADIO_HTTP_INFO_SIZE];
    char discover_info_text[FUSE_RADIO_DISCOVER_INFO_SIZE];
    char mdns_info_text[FUSE_RADIO_MDNS_INFO_SIZE];
    char promiscuous_info_text[FUSE_RADIO_PROMISCUOUS_INFO_SIZE];
    char promiscuous_live_text[FUSE_RADIO_PROMISCUOUS_LIVE_SIZE];
    uint16_t wifi_status_reason;
    uint16_t discover_scanned_count;
    uint16_t discover_found_count;
    uint32_t discover_duration_ms;
    uint32_t promiscuous_live_elapsed_ms;
    uint32_t promiscuous_live_total_frames;
    uint16_t promiscuous_live_unique_count;
    uint16_t promiscuous_live_beacon_count;
    int16_t promiscuous_live_max_rssi;
    uint8_t mdns_count;
    uint8_t mdns_truncated_count;
    uint8_t promiscuous_watch_channel;
    uint8_t survey_selected_channel_index;
    uint8_t watch_device_count;
    uint8_t saved_credential_count;
    bool wifi_connected;
    bool connect_password_saved;
    bool connect_password_auto_used;
    bool connect_flow_active;
    bool promiscuous_watch_live_active;
    bool promiscuous_watch_stop_pending;

    FuseRadioSavedCredential saved_credentials[FUSE_RADIO_MAX_SAVED_NETWORKS];

    uint32_t detect_started_at;
    uint32_t last_ping_at;
    uint32_t last_mode_guard_poll_at;
    uint32_t next_startup_action_at;
    uint8_t startup_link_retry_count;
    uint8_t startup_power_retry_count;
    bool rx_started;
    bool expansion_disabled;
    bool otg_enabled;
    bool insomnia_active;
    bool module_detect_event_sent;
    bool status_dirty;
    bool wifi_info_dirty;
    bool http_dirty;
    bool discover_dirty;
    bool mdns_dirty;
    bool promiscuous_dirty;
    bool scan_dirty;
};

void fuse_radio_app_set_status(FuseRadioApp* app, const char* detail);
void fuse_radio_app_set_error(FuseRadioApp* app, const char* error);
void fuse_radio_app_text_input_callback(void* context);
bool fuse_radio_app_start_session(FuseRadioApp* app);
void fuse_radio_app_stop_session(FuseRadioApp* app);
void fuse_radio_app_retry_session(FuseRadioApp* app);
bool fuse_radio_app_send_ping(FuseRadioApp* app);
bool fuse_radio_app_start_wifi_scan(FuseRadioApp* app);
bool fuse_radio_app_request_wifi_status(FuseRadioApp* app);
bool fuse_radio_app_start_wifi_connect(FuseRadioApp* app);
bool fuse_radio_app_start_wifi_disconnect(FuseRadioApp* app);
bool fuse_radio_app_start_wifi_discover(FuseRadioApp* app);
bool fuse_radio_app_start_wifi_http_request(FuseRadioApp* app, FuseRadioHttpPreset preset);
bool fuse_radio_app_start_wifi_mdns_query(FuseRadioApp* app);
bool fuse_radio_app_start_wifi_promiscuous_enter(FuseRadioApp* app, uint8_t channel);
bool fuse_radio_app_start_wifi_promiscuous_exit(FuseRadioApp* app);
bool fuse_radio_app_start_wifi_promiscuous_survey(FuseRadioApp* app, FuseRadioSurveyPreset preset);
bool fuse_radio_app_start_wifi_promiscuous_watch(FuseRadioApp* app, uint8_t channel);
bool fuse_radio_app_stop_wifi_promiscuous_watch(FuseRadioApp* app);
bool fuse_radio_app_repeat_wifi_promiscuous_action(FuseRadioApp* app);
void fuse_radio_app_process_rx(FuseRadioApp* app);
void fuse_radio_app_handle_tick(FuseRadioApp* app);
void fuse_radio_app_refresh_status_widget(FuseRadioApp* app);
void fuse_radio_app_refresh_wifi_info_widget(FuseRadioApp* app);
void fuse_radio_app_refresh_http_widget(FuseRadioApp* app);
void fuse_radio_app_refresh_discover_widget(FuseRadioApp* app);
void fuse_radio_app_refresh_mdns_widget(FuseRadioApp* app);
void fuse_radio_app_refresh_promiscuous_widget(FuseRadioApp* app);
void fuse_radio_app_refresh_scan_view(FuseRadioApp* app);
const char* fuse_radio_app_get_saved_password(FuseRadioApp* app, const char* ssid);
bool fuse_radio_app_store_saved_credential(FuseRadioApp* app, const char* ssid, const char* password);
