#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FUSE_RADIO_MAX_AP_COUNT    20U
#define FUSE_RADIO_MAX_SSID_LENGTH 32U
#define FUSE_RADIO_MAX_AUTH_LENGTH 15U
#define FUSE_RADIO_SCAN_ERROR_SIZE 32U
#define FUSE_RADIO_MAX_DISCOVER_HOSTS 20U
#define FUSE_RADIO_DISCOVER_HOSTNAME_SIZE 48U
#define FUSE_RADIO_DISCOVER_VENDOR_SIZE 20U
#define FUSE_RADIO_DISCOVER_ROLE_SIZE 8U
#define FUSE_RADIO_DISCOVER_SERVICES_SIZE 24U
#define FUSE_RADIO_DISCOVER_SUBNET_SIZE 24U
#define FUSE_RADIO_MAX_BLE_DEVICES 20U
#define FUSE_RADIO_MAX_SAVED_BLE_DEVICES 20U
#define FUSE_RADIO_BLE_NAME_SIZE 32U
#define FUSE_RADIO_BLE_ADDR_TYPE_SIZE 12U
#define FUSE_RADIO_BLE_COMPANY_SIZE 20U
#define FUSE_RADIO_BLE_APPEARANCE_SIZE 18U
#define FUSE_RADIO_BLE_CLASS_SIZE 16U
#define FUSE_RADIO_BLE_PROXIMITY_SIZE 10U
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
    FuseRadioWifiActionRequestingHttp,
    FuseRadioWifiActionEnteringPromiscuous,
    FuseRadioWifiActionExitingPromiscuous,
    FuseRadioWifiActionSurveying,
    FuseRadioWifiActionWatching,
    FuseRadioWifiActionBeaconing,
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

typedef enum {
    FuseRadioDiscoverNameSourceNone,
    FuseRadioDiscoverNameSourceMdns,
    FuseRadioDiscoverNameSourceReverseDns,
} FuseRadioDiscoverNameSource;

typedef struct {
    char ip[16];
    char name[FUSE_RADIO_DISCOVER_HOSTNAME_SIZE];
    char mac[18];
    char vendor[FUSE_RADIO_DISCOVER_VENDOR_SIZE];
    char role[FUSE_RADIO_DISCOVER_ROLE_SIZE];
    char services[FUSE_RADIO_DISCOVER_SERVICES_SIZE];
    uint16_t rtt_ms;
    FuseRadioDiscoverNameSource name_source;
    bool has_name;
    bool has_mac;
    bool has_vendor;
    bool has_role;
    bool has_services;
    bool is_gateway;
} FuseRadioDiscoverHost;

typedef struct {
    FuseRadioDiscoverHost hosts[FUSE_RADIO_MAX_DISCOVER_HOSTS];
    uint8_t count;
    uint16_t total_hosts;
    uint16_t scanned_count;
    uint16_t found_count;
    uint16_t truncated_count;
    uint8_t progress_percent;
    uint32_t duration_ms;
    char subnet[FUSE_RADIO_DISCOVER_SUBNET_SIZE];
    char self_ip[16];
    char gateway_ip[16];
    char current_ip[16];
    bool active;
    bool complete;
    bool has_error;
    char error[FUSE_RADIO_SCAN_ERROR_SIZE];
} FuseRadioDiscoverResults;

typedef struct {
    char mac[18];
    char name[FUSE_RADIO_BLE_NAME_SIZE];
    char addr_type[FUSE_RADIO_BLE_ADDR_TYPE_SIZE];
    char company[FUSE_RADIO_BLE_COMPANY_SIZE];
    char appearance[FUSE_RADIO_BLE_APPEARANCE_SIZE];
    char classification[FUSE_RADIO_BLE_CLASS_SIZE];
    char proximity[FUSE_RADIO_BLE_PROXIMITY_SIZE];
    int16_t rssi;
    bool has_name;
    bool has_company;
    bool has_appearance;
    bool connectable;
} FuseRadioBleDevice;

typedef struct {
    FuseRadioBleDevice devices[FUSE_RADIO_MAX_BLE_DEVICES];
    uint8_t count;
    uint16_t total_count;
    uint16_t truncated_count;
    uint16_t scan_duration_ms;
    uint8_t progress_percent;
    bool active;
    bool complete;
    bool has_error;
    char error[FUSE_RADIO_SCAN_ERROR_SIZE];
} FuseRadioBleScanResults;

typedef struct {
    char mac[18];
    char name[FUSE_RADIO_BLE_NAME_SIZE];
    char addr_type[FUSE_RADIO_BLE_ADDR_TYPE_SIZE];
    char company[FUSE_RADIO_BLE_COMPANY_SIZE];
    char appearance[FUSE_RADIO_BLE_APPEARANCE_SIZE];
    char classification[FUSE_RADIO_BLE_CLASS_SIZE];
    char proximity[FUSE_RADIO_BLE_PROXIMITY_SIZE];
    int16_t last_rssi;
    bool has_name;
    bool has_company;
    bool has_appearance;
    bool connectable;
    bool seen_recently;
} FuseRadioSavedBleDevice;

typedef struct {
    FuseRadioSavedBleDevice devices[FUSE_RADIO_MAX_SAVED_BLE_DEVICES];
    uint8_t count;
} FuseRadioSavedBleResults;

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
