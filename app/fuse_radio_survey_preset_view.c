#include "fuse_radio_survey_preset_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdlib.h>

struct FuseRadioSurveyPresetView {
    View* view;
    FuseRadioSurveyPresetViewCallback callback;
    void* context;
};

typedef struct {
    FuseRadioSurveyPreset preset;
} FuseRadioSurveyPresetViewModel;

typedef struct {
    const char* title;
    const char* detail;
} FuseRadioSurveyPresetMeta;

static const FuseRadioSurveyPresetMeta fuse_radio_survey_preset_meta[] = {
    [FuseRadioSurveyPreset11611] = {.title = "1 / 6 / 11", .detail = "Fast tri-band check"},
    [FuseRadioSurveyPreset111] = {.title = "1 - 11", .detail = "Full 2.4 GHz sweep"},
    [FuseRadioSurveyPreset14814] = {.title = "1 / 4 / 8 / 14", .detail = "Wide spaced sample"},
    [FuseRadioSurveyPreset1357911] = {.title = "Odd channels", .detail = "1 3 5 7 9 11"},
};

static FuseRadioSurveyPreset fuse_radio_survey_preset_next(FuseRadioSurveyPreset preset, int8_t step) {
    int32_t value = (int32_t)preset + step;

    if(value < 0) {
        value = (int32_t)FuseRadioSurveyPresetCount - 1;
    } else if(value >= (int32_t)FuseRadioSurveyPresetCount) {
        value = 0;
    }

    return (FuseRadioSurveyPreset)value;
}

static void fuse_radio_survey_preset_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioSurveyPresetViewModel* view_model = model;
    const FuseRadioSurveyPresetMeta* meta = &fuse_radio_survey_preset_meta[view_model->preset];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 17, 12, "Survey Pattern");
    canvas_draw_line(canvas, 0, 15, 127, 15);

    canvas_set_font(canvas, FontPrimary);
    elements_multiline_text_aligned(canvas, 64, 33, AlignCenter, AlignCenter, meta->title);
    canvas_set_font(canvas, FontSecondary);
    elements_multiline_text_aligned(canvas, 64, 49, AlignCenter, AlignCenter, meta->detail);
    elements_button_center(canvas, "Start");
}

static bool fuse_radio_survey_preset_view_input_callback(InputEvent* event, void* context) {
    FuseRadioSurveyPresetView* instance = context;
    bool consumed = false;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioSurveyPresetViewModel * model,
        {
            if(event->key == InputKeyUp) {
                model->preset = fuse_radio_survey_preset_next(model->preset, 1);
                consumed = true;
            } else if(event->key == InputKeyDown) {
                model->preset = fuse_radio_survey_preset_next(model->preset, -1);
                consumed = true;
            } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                consumed = true;
            }
        },
        consumed);

    if(consumed && event->key == InputKeyOk && event->type == InputTypeShort && instance->callback) {
        instance->callback(fuse_radio_survey_preset_view_get_preset(instance), instance->context);
    }

    return consumed;
}

FuseRadioSurveyPresetView* fuse_radio_survey_preset_view_alloc(void) {
    FuseRadioSurveyPresetView* instance = malloc(sizeof(FuseRadioSurveyPresetView));

    instance->view = view_alloc();
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(FuseRadioSurveyPresetViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_survey_preset_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_survey_preset_view_input_callback);
    fuse_radio_survey_preset_view_set_preset(instance, FuseRadioSurveyPreset11611);

    return instance;
}

void fuse_radio_survey_preset_view_free(FuseRadioSurveyPresetView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_survey_preset_view_get_view(FuseRadioSurveyPresetView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_survey_preset_view_set_callback(
    FuseRadioSurveyPresetView* instance,
    FuseRadioSurveyPresetViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_survey_preset_view_set_preset(
    FuseRadioSurveyPresetView* instance,
    FuseRadioSurveyPreset preset) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FuseRadioSurveyPresetViewModel * model,
        { model->preset = preset; },
        true);
}

FuseRadioSurveyPreset fuse_radio_survey_preset_view_get_preset(FuseRadioSurveyPresetView* instance) {
    FuseRadioSurveyPreset preset = FuseRadioSurveyPreset11611;

    furi_assert(instance);
    with_view_model(
        instance->view,
        FuseRadioSurveyPresetViewModel * model,
        { preset = model->preset; },
        false);

    return preset;
}