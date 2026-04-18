#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FUSE_RADIO_MAX_AP_COUNT    20U
#define FUSE_RADIO_MAX_SSID_LENGTH 32U
#define FUSE_RADIO_MAX_AUTH_LENGTH 15U
#define FUSE_RADIO_SCAN_ERROR_SIZE 32U
#define FUSE_RADIO_MAX_WATCH_DEVICES 12U
#define FUSE_RADIO_MAX_SURVEY_CHANNELS 14U

typedef struct FuseRadioApp FuseRadioApp;

typedef enum {
    FuseRadioWifiStateIdle,
    FuseRadioWifiStateScanRequested,
    FuseRadioWifiStateScanning,
    FuseRadioWifiStateScanComplete,
    FuseRadioWifiStateConnectRequested,
    FuseRadioWifiStateConnecting,
    FuseRadioWifiStateConnected,
    FuseRadioWifiStateDisconnecting,
    FuseRadioWifiStateError,
} FuseRadioWifiState;

typedef enum {
    FuseRadioWifiModeUnknown,
    FuseRadioWifiModeIdle,
    FuseRadioWifiModeConnected,
    FuseRadioWifiModePromiscuous,
} FuseRadioWifiMode;

typedef enum {
    FuseRadioWifiActionUnknown,
    FuseRadioWifiActionNone,
    FuseRadioWifiActionScanning,
    FuseRadioWifiActionConnecting,
    FuseRadioWifiActionDisconnecting,
    FuseRadioWifiActionDiscovering,
    FuseRadioWifiActionResolvingMdns,
    FuseRadioWifiActionEnteringPromiscuous,
    FuseRadioWifiActionExitingPromiscuous,
    FuseRadioWifiActionSurveying,
    FuseRadioWifiActionWatching,
} FuseRadioWifiAction;

typedef struct {
    char ssid[FUSE_RADIO_MAX_SSID_LENGTH + 1U];
    char auth[FUSE_RADIO_MAX_AUTH_LENGTH + 1U];
    int16_t rssi;
    uint8_t channel;
} FuseRadioAccessPoint;

typedef struct {
    FuseRadioAccessPoint aps[FUSE_RADIO_MAX_AP_COUNT];
    uint8_t count;
    uint16_t total_count;
    uint16_t truncated_count;
    bool is_complete;
    bool has_error;
    char error[FUSE_RADIO_SCAN_ERROR_SIZE];
} FuseRadioScanResults;

typedef struct {
    char mac[18];
    uint32_t frames;
    uint16_t beacons;
    int16_t max_rssi;
    bool likely_ap;
} FuseRadioWatchDevice;

typedef struct {
    uint8_t channel;
    uint32_t duration_ms;
    uint32_t total_frames;
    uint32_t management_frames;
    uint32_t data_frames;
    uint32_t control_frames;
    uint32_t misc_frames;
    uint16_t beacon_frames;
    uint16_t deauth_frames;
    uint16_t unique_count;
    int16_t max_rssi;
    bool has_summary;
} FuseRadioWatchSummary;

typedef enum {
    FuseRadioSurveyPreset11611,
    FuseRadioSurveyPreset111,
    FuseRadioSurveyPreset14814,
    FuseRadioSurveyPreset1357911,
    FuseRadioSurveyPresetCount,
} FuseRadioSurveyPreset;

typedef struct {
    uint8_t channel;
    uint32_t total_frames;
    uint32_t management_frames;
    uint32_t data_frames;
    uint32_t control_frames;
    uint32_t misc_frames;
    uint16_t beacon_frames;
    uint16_t deauth_frames;
    uint16_t unique_count;
    int16_t max_rssi;
    uint32_t duration_ms;
    bool has_result;
} FuseRadioSurveyChannelResult;

typedef struct {
    FuseRadioSurveyPreset preset;
    FuseRadioSurveyChannelResult channels[FUSE_RADIO_MAX_SURVEY_CHANNELS];
    uint8_t result_count;
    uint8_t expected_count;
    uint8_t current_channel;
    uint8_t completed_count;
    uint8_t recommended_channel;
    uint8_t progress_percent;
    uint16_t dwell_ms;
    uint32_t duration_ms;
    bool active;
    bool complete;
} FuseRadioSurveyResults;

FuseRadioApp* fuse_radio_app_alloc(void);
void fuse_radio_app_free(FuseRadioApp* app);
void fuse_radio_app_run(FuseRadioApp* app);
