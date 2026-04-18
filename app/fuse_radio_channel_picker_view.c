#include "fuse_radio_channel_picker_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>

#define FUSE_RADIO_CHANNEL_PICKER_MIN_CHANNEL 1U
#define FUSE_RADIO_CHANNEL_PICKER_MAX_CHANNEL 14U

struct FuseRadioChannelPickerView {
    View* view;
    FuseRadioChannelPickerViewCallback callback;
    void* context;
};

typedef struct {
    uint8_t channel;
} FuseRadioChannelPickerViewModel;

static uint8_t fuse_radio_channel_picker_view_clamp(uint8_t channel) {
    if(channel < FUSE_RADIO_CHANNEL_PICKER_MIN_CHANNEL ||
       channel > FUSE_RADIO_CHANNEL_PICKER_MAX_CHANNEL) {
        return FUSE_RADIO_CHANNEL_PICKER_MIN_CHANNEL;
    }

    return channel;
}

static void fuse_radio_channel_picker_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioChannelPickerViewModel* view_model = model;
    char channel_text[8];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 18, 12, "Watch Channel");
    canvas_draw_line(canvas, 0, 15, 127, 15);

    canvas_set_font(canvas, FontSecondary);
    elements_multiline_text_aligned(
        canvas, 64, 26, AlignCenter, AlignTop, "Up/Down selects a channel");

    canvas_set_font(canvas, FontPrimary);
    snprintf(channel_text, sizeof(channel_text), "%u", (unsigned)view_model->channel);
    elements_multiline_text_aligned(
        canvas, 64, 42, AlignCenter, AlignCenter, channel_text);

    elements_button_center(canvas, "Start");
}

static bool fuse_radio_channel_picker_view_input_callback(InputEvent* event, void* context) {
    FuseRadioChannelPickerView* instance = context;
    bool consumed = false;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioChannelPickerViewModel * model,
        {
            if(event->key == InputKeyUp) {
                model->channel =
                    (model->channel >= FUSE_RADIO_CHANNEL_PICKER_MAX_CHANNEL) ?
                        FUSE_RADIO_CHANNEL_PICKER_MIN_CHANNEL :
                        (uint8_t)(model->channel + 1U);
                consumed = true;
            } else if(event->key == InputKeyDown) {
                model->channel =
                    (model->channel <= FUSE_RADIO_CHANNEL_PICKER_MIN_CHANNEL) ?
                        FUSE_RADIO_CHANNEL_PICKER_MAX_CHANNEL :
                        (uint8_t)(model->channel - 1U);
                consumed = true;
            } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                consumed = true;
            }
        },
        consumed);

    if(consumed && event->key == InputKeyOk && event->type == InputTypeShort && instance->callback) {
        instance->callback(fuse_radio_channel_picker_view_get_channel(instance), instance->context);
    }

    return consumed;
}

FuseRadioChannelPickerView* fuse_radio_channel_picker_view_alloc(void) {
    FuseRadioChannelPickerView* instance = malloc(sizeof(FuseRadioChannelPickerView));

    instance->view = view_alloc();
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(FuseRadioChannelPickerViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_channel_picker_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_channel_picker_view_input_callback);

    fuse_radio_channel_picker_view_set_channel(instance, FUSE_RADIO_CHANNEL_PICKER_MIN_CHANNEL);
    return instance;
}

void fuse_radio_channel_picker_view_free(FuseRadioChannelPickerView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_channel_picker_view_get_view(FuseRadioChannelPickerView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_channel_picker_view_set_callback(
    FuseRadioChannelPickerView* instance,
    FuseRadioChannelPickerViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_channel_picker_view_set_channel(
    FuseRadioChannelPickerView* instance,
    uint8_t channel) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        FuseRadioChannelPickerViewModel * model,
        { model->channel = fuse_radio_channel_picker_view_clamp(channel); },
        true);
}

uint8_t fuse_radio_channel_picker_view_get_channel(FuseRadioChannelPickerView* instance) {
    furi_assert(instance);

    uint8_t channel = FUSE_RADIO_CHANNEL_PICKER_MIN_CHANNEL;
    with_view_model(
        instance->view,
        FuseRadioChannelPickerViewModel * model,
        { channel = model->channel; },
        false);
    return channel;
}