#include "fuse_radio_value_picker_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUSE_RADIO_VALUE_PICKER_MIN_VALUE 0U
#define FUSE_RADIO_VALUE_PICKER_MAX_VALUE 16U

struct FuseRadioValuePickerView {
    View* view;
    FuseRadioValuePickerViewCallback callback;
    void* context;
};

typedef struct {
    uint8_t value;
    char title[24];
} FuseRadioValuePickerViewModel;

static uint8_t fuse_radio_value_picker_view_clamp(uint8_t value) {
    if(value > FUSE_RADIO_VALUE_PICKER_MAX_VALUE) {
        return FUSE_RADIO_VALUE_PICKER_MAX_VALUE;
    }

    return value;
}

static void fuse_radio_value_picker_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioValuePickerViewModel* view_model = model;
    char value_text[8];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 10, 12, view_model->title);
    canvas_draw_line(canvas, 0, 15, 127, 15);

    canvas_set_font(canvas, FontSecondary);
    elements_multiline_text_aligned(canvas, 64, 24, AlignCenter, AlignTop, "Up/Down adjusts 0..16");

    canvas_set_font(canvas, FontPrimary);
    snprintf(value_text, sizeof(value_text), "%u", (unsigned)view_model->value);
    elements_multiline_text_aligned(canvas, 64, 42, AlignCenter, AlignCenter, value_text);

    elements_button_center(canvas, "Save");
}

static bool fuse_radio_value_picker_view_input_callback(InputEvent* event, void* context) {
    FuseRadioValuePickerView* instance = context;
    bool consumed = false;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioValuePickerViewModel * model,
        {
            if(event->key == InputKeyUp) {
                model->value =
                    (model->value >= FUSE_RADIO_VALUE_PICKER_MAX_VALUE) ?
                        FUSE_RADIO_VALUE_PICKER_MIN_VALUE :
                        (uint8_t)(model->value + 1U);
                consumed = true;
            } else if(event->key == InputKeyDown) {
                model->value =
                    (model->value <= FUSE_RADIO_VALUE_PICKER_MIN_VALUE) ?
                        FUSE_RADIO_VALUE_PICKER_MAX_VALUE :
                        (uint8_t)(model->value - 1U);
                consumed = true;
            } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                consumed = true;
            }
        },
        consumed);

    if(consumed && event->key == InputKeyOk && event->type == InputTypeShort && instance->callback) {
        instance->callback(fuse_radio_value_picker_view_get_value(instance), instance->context);
    }

    return consumed;
}

FuseRadioValuePickerView* fuse_radio_value_picker_view_alloc(void) {
    FuseRadioValuePickerView* instance = malloc(sizeof(FuseRadioValuePickerView));

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FuseRadioValuePickerViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_value_picker_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_value_picker_view_input_callback);

    fuse_radio_value_picker_view_set_title(instance, "LED Value");
    fuse_radio_value_picker_view_set_value(instance, FUSE_RADIO_VALUE_PICKER_MIN_VALUE);
    return instance;
}

void fuse_radio_value_picker_view_free(FuseRadioValuePickerView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_value_picker_view_get_view(FuseRadioValuePickerView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_value_picker_view_set_callback(
    FuseRadioValuePickerView* instance,
    FuseRadioValuePickerViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_value_picker_view_set_title(FuseRadioValuePickerView* instance, const char* title) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        FuseRadioValuePickerViewModel * model,
        {
            strncpy(model->title, title, sizeof(model->title) - 1U);
            model->title[sizeof(model->title) - 1U] = '\0';
        },
        true);
}

void fuse_radio_value_picker_view_set_value(FuseRadioValuePickerView* instance, uint8_t value) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        FuseRadioValuePickerViewModel * model,
        { model->value = fuse_radio_value_picker_view_clamp(value); },
        true);
}

uint8_t fuse_radio_value_picker_view_get_value(FuseRadioValuePickerView* instance) {
    uint8_t value = FUSE_RADIO_VALUE_PICKER_MIN_VALUE;

    furi_assert(instance);

    with_view_model(
        instance->view,
        FuseRadioValuePickerViewModel * model,
        { value = model->value; },
        false);
    return value;
}