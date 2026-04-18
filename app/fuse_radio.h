#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FUSE_RADIO_MAX_AP_COUNT    20U
#define FUSE_RADIO_MAX_SSID_LENGTH 32U
#define FUSE_RADIO_MAX_AUTH_LENGTH 15U
#define FUSE_RADIO_SCAN_ERROR_SIZE 32U

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

FuseRadioApp* fuse_radio_app_alloc(void);
void fuse_radio_app_free(FuseRadioApp* app);
void fuse_radio_app_run(FuseRadioApp* app);
