#pragma once

#include <gui/view.h>
#include <input/input.h>

typedef struct FuseRadioChannelPickerView FuseRadioChannelPickerView;

typedef void (*FuseRadioChannelPickerViewCallback)(uint8_t channel, void* context);

FuseRadioChannelPickerView* fuse_radio_channel_picker_view_alloc(void);
void fuse_radio_channel_picker_view_free(FuseRadioChannelPickerView* instance);
View* fuse_radio_channel_picker_view_get_view(FuseRadioChannelPickerView* instance);
void fuse_radio_channel_picker_view_set_callback(
    FuseRadioChannelPickerView* instance,
    FuseRadioChannelPickerViewCallback callback,
    void* context);
void fuse_radio_channel_picker_view_set_channel(
    FuseRadioChannelPickerView* instance,
    uint8_t channel);
uint8_t fuse_radio_channel_picker_view_get_channel(FuseRadioChannelPickerView* instance);