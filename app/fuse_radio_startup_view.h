#pragma once

#include "fuse_radio.h"

#include <gui/view.h>
#include <input/input.h>
#include <stdbool.h>

typedef struct FuseRadioStartupView FuseRadioStartupView;

typedef enum {
    FuseRadioStartupStateBooting,
    FuseRadioStartupStateError,
    FuseRadioStartupStateReady,
} FuseRadioStartupState;

typedef void (*FuseRadioStartupViewCallback)(void* context);

FuseRadioStartupView* fuse_radio_startup_view_alloc(void);
void fuse_radio_startup_view_free(FuseRadioStartupView* instance);
View* fuse_radio_startup_view_get_view(FuseRadioStartupView* instance);
void fuse_radio_startup_view_set_callback(
    FuseRadioStartupView* instance,
    FuseRadioStartupViewCallback callback,
    void* context);
void fuse_radio_startup_view_set_state(
    FuseRadioStartupView* instance,
    FuseRadioStartupState state);
void fuse_radio_startup_view_advance_animation(FuseRadioStartupView* instance);
