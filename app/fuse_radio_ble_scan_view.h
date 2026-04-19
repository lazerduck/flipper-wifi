#pragma once

#include "fuse_radio.h"

#include <gui/view.h>
#include <input/input.h>

typedef enum {
    FuseRadioBleScanViewModeScan,
    FuseRadioBleScanViewModeSaved,
} FuseRadioBleScanViewMode;

typedef enum {
    FuseRadioBleScanViewActionRefresh,
    FuseRadioBleScanViewActionSave,
} FuseRadioBleScanViewAction;

typedef void (*FuseRadioBleScanViewCallback)(FuseRadioBleScanViewAction action, void* context);

typedef struct FuseRadioBleScanView FuseRadioBleScanView;

FuseRadioBleScanView* fuse_radio_ble_scan_view_alloc(void);
void fuse_radio_ble_scan_view_free(FuseRadioBleScanView* instance);
View* fuse_radio_ble_scan_view_get_view(FuseRadioBleScanView* instance);
void fuse_radio_ble_scan_view_set_callback(
    FuseRadioBleScanView* instance,
    FuseRadioBleScanViewCallback callback,
    void* context);
void fuse_radio_ble_scan_view_set_mode(
    FuseRadioBleScanView* instance,
    FuseRadioBleScanViewMode mode);
void fuse_radio_ble_scan_view_set_scan_data(
    FuseRadioBleScanView* instance,
    const FuseRadioBleScanResults* results);
void fuse_radio_ble_scan_view_set_saved_data(
    FuseRadioBleScanView* instance,
    const FuseRadioSavedBleResults* results);
uint8_t fuse_radio_ble_scan_view_get_selected_index(FuseRadioBleScanView* instance);
void fuse_radio_ble_scan_view_advance_animation(FuseRadioBleScanView* instance);