#pragma once

#include "fuse_radio.h"

#include <gui/view.h>

typedef enum {
    FuseRadioDiscoverResultViewActionRepeat,
} FuseRadioDiscoverResultViewAction;

typedef void (*FuseRadioDiscoverResultViewCallback)(
    FuseRadioDiscoverResultViewAction action,
    void* context);

typedef struct FuseRadioDiscoverResultView FuseRadioDiscoverResultView;

FuseRadioDiscoverResultView* fuse_radio_discover_result_view_alloc(void);
void fuse_radio_discover_result_view_free(FuseRadioDiscoverResultView* instance);
View* fuse_radio_discover_result_view_get_view(FuseRadioDiscoverResultView* instance);
void fuse_radio_discover_result_view_set_callback(
    FuseRadioDiscoverResultView* instance,
    FuseRadioDiscoverResultViewCallback callback,
    void* context);
void fuse_radio_discover_result_view_set_data(
    FuseRadioDiscoverResultView* instance,
    const FuseRadioDiscoverResults* results);