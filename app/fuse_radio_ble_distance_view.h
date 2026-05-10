#pragma once

#include <gui/view.h>
#include <input/input.h>

#define FUSE_RADIO_BLE_DISTANCE_VIEW_HISTORY 64U

typedef struct FuseRadioBleDistanceView FuseRadioBleDistanceView;

typedef enum {
    FuseRadioBleDistanceViewActionStop,
    FuseRadioBleDistanceViewActionBack,
} FuseRadioBleDistanceViewAction;

typedef void (*FuseRadioBleDistanceViewCallback)(FuseRadioBleDistanceViewAction action, void* context);

typedef struct {
    char     header[24];
    int16_t  rssi;
    bool     seen;
    bool     active;
    bool     stop_pending;
    char     trend[10];
    uint16_t samples;
    int8_t   history[FUSE_RADIO_BLE_DISTANCE_VIEW_HISTORY];
    uint8_t  history_head;   /* index of next write slot */
    uint8_t  history_count;  /* number of valid entries (0..HISTORY) */
} FuseRadioBleDistanceSnapshot;

FuseRadioBleDistanceView* fuse_radio_ble_distance_view_alloc(void);
void fuse_radio_ble_distance_view_free(FuseRadioBleDistanceView* instance);
View* fuse_radio_ble_distance_view_get_view(FuseRadioBleDistanceView* instance);
void fuse_radio_ble_distance_view_set_callback(
    FuseRadioBleDistanceView* instance,
    FuseRadioBleDistanceViewCallback callback,
    void* context);
void fuse_radio_ble_distance_view_set_snapshot(
    FuseRadioBleDistanceView* instance,
    const FuseRadioBleDistanceSnapshot* snapshot);
