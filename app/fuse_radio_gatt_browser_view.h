#pragma once

#include "fuse_radio.h"

#include <gui/view.h>
#include <input/input.h>

typedef struct FuseRadioGattBrowserView FuseRadioGattBrowserView;

FuseRadioGattBrowserView* fuse_radio_gatt_browser_view_alloc(void);
void fuse_radio_gatt_browser_view_free(FuseRadioGattBrowserView* instance);
View* fuse_radio_gatt_browser_view_get_view(FuseRadioGattBrowserView* instance);

void fuse_radio_gatt_browser_view_set_data(
    FuseRadioGattBrowserView* instance,
    const FuseRadioGattResults* results,
    uint8_t preferred_service_index);
