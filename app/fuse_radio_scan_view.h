#pragma once

#include "fuse_radio.h"

#include <gui/view.h>
#include <input/input.h>

typedef enum {
    FuseRadioScanViewActionRescan,
    FuseRadioScanViewActionSelect,
} FuseRadioScanViewAction;

typedef enum {
    FuseRadioScanViewModeBrowse,
    FuseRadioScanViewModeSelect,
} FuseRadioScanViewMode;

typedef void (*FuseRadioScanViewCallback)(FuseRadioScanViewAction action, void* context);

typedef struct FuseRadioScanView FuseRadioScanView;

FuseRadioScanView* fuse_radio_scan_view_alloc(void);
void fuse_radio_scan_view_free(FuseRadioScanView* instance);
View* fuse_radio_scan_view_get_view(FuseRadioScanView* instance);
void fuse_radio_scan_view_set_callback(
    FuseRadioScanView* instance,
    FuseRadioScanViewCallback callback,
    void* context);
void fuse_radio_scan_view_set_mode(FuseRadioScanView* instance, FuseRadioScanViewMode mode);
uint8_t fuse_radio_scan_view_get_selected_index(FuseRadioScanView* instance);
void fuse_radio_scan_view_set_data(
    FuseRadioScanView* instance,
    const FuseRadioScanResults* results,
    FuseRadioWifiState wifi_state);
void fuse_radio_scan_view_advance_animation(FuseRadioScanView* instance);