#pragma once

#include <gui/view.h>
#include <input/input.h>

typedef struct FuseRadioValuePickerView FuseRadioValuePickerView;

typedef void (*FuseRadioValuePickerViewCallback)(uint8_t value, void* context);

FuseRadioValuePickerView* fuse_radio_value_picker_view_alloc(void);
void fuse_radio_value_picker_view_free(FuseRadioValuePickerView* instance);
View* fuse_radio_value_picker_view_get_view(FuseRadioValuePickerView* instance);
void fuse_radio_value_picker_view_set_callback(
    FuseRadioValuePickerView* instance,
    FuseRadioValuePickerViewCallback callback,
    void* context);
void fuse_radio_value_picker_view_set_title(FuseRadioValuePickerView* instance, const char* title);
void fuse_radio_value_picker_view_set_value(FuseRadioValuePickerView* instance, uint8_t value);
uint8_t fuse_radio_value_picker_view_get_value(FuseRadioValuePickerView* instance);