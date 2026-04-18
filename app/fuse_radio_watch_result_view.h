#pragma once

#include "fuse_radio.h"

#include <gui/view.h>
#include <input/input.h>

typedef enum {
    FuseRadioWatchResultViewActionRepeat,
} FuseRadioWatchResultViewAction;

typedef void (*FuseRadioWatchResultViewCallback)(
    FuseRadioWatchResultViewAction action,
    void* context);

typedef struct FuseRadioWatchResultView FuseRadioWatchResultView;

FuseRadioWatchResultView* fuse_radio_watch_result_view_alloc(void);
void fuse_radio_watch_result_view_free(FuseRadioWatchResultView* instance);
View* fuse_radio_watch_result_view_get_view(FuseRadioWatchResultView* instance);
void fuse_radio_watch_result_view_set_callback(
    FuseRadioWatchResultView* instance,
    FuseRadioWatchResultViewCallback callback,
    void* context);
void fuse_radio_watch_result_view_set_data(
    FuseRadioWatchResultView* instance,
    const FuseRadioWatchSummary* summary,
    const FuseRadioWatchDevice* devices,
    uint8_t count);