#pragma once

#include <gui/view.h>
#include <input/input.h>

typedef struct FuseRadioWatchLiveView FuseRadioWatchLiveView;

typedef void (*FuseRadioWatchLiveViewCallback)(void* context);

typedef struct {
    uint8_t channel;
    uint32_t elapsed_ms;
    uint32_t total_frames;
    uint16_t unique_count;
    uint16_t beacon_count;
    int16_t max_rssi;
    bool stop_pending;
} FuseRadioWatchLiveSnapshot;

FuseRadioWatchLiveView* fuse_radio_watch_live_view_alloc(void);
void fuse_radio_watch_live_view_free(FuseRadioWatchLiveView* instance);
View* fuse_radio_watch_live_view_get_view(FuseRadioWatchLiveView* instance);
void fuse_radio_watch_live_view_set_callback(
    FuseRadioWatchLiveView* instance,
    FuseRadioWatchLiveViewCallback callback,
    void* context);
void fuse_radio_watch_live_view_set_snapshot(
    FuseRadioWatchLiveView* instance,
    const FuseRadioWatchLiveSnapshot* snapshot);