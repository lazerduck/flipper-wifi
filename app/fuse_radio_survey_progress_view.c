#include "fuse_radio_survey_progress_view.h"

#include <gui/canvas.h>

#include <stdio.h>
#include <stdlib.h>

struct FuseRadioSurveyProgressView {
    View* view;
};

typedef struct {
    FuseRadioSurveyProgressSnapshot snapshot;
} FuseRadioSurveyProgressViewModel;

static void fuse_radio_survey_progress_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioSurveyProgressViewModel* view_model = model;
    char line[24];
    char percent[8];
    char counter[12];
    uint8_t fill_width = (uint8_t)((108U * view_model->snapshot.progress_percent) / 100U);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 10, "Survey scan");
    canvas_draw_line(canvas, 0, 13, 127, 13);

    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "Scanning ch%u", (unsigned)view_model->snapshot.current_channel);
    canvas_draw_str(canvas, 8, 27, line);

    snprintf(percent, sizeof(percent), "%u%%", (unsigned)view_model->snapshot.progress_percent);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 95, 27, percent);

    canvas_draw_rframe(canvas, 9, 33, 110, 11, 2);
    if(fill_width > 0U) {
        canvas_draw_box(canvas, 10, 34, fill_width, 9);
    }

    canvas_set_font(canvas, FontSecondary);
    snprintf(
        counter,
        sizeof(counter),
        "%u/%u",
        (unsigned)view_model->snapshot.completed_count,
        (unsigned)view_model->snapshot.total_count);
    canvas_draw_str(canvas, 8, 56, counter);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(
        canvas,
        39,
        56,
        (view_model->snapshot.animation_frame % 3U) == 0U ? "Listening." :
            ((view_model->snapshot.animation_frame % 3U) == 1U ? "Listening.." : "Listening..."));
}

FuseRadioSurveyProgressView* fuse_radio_survey_progress_view_alloc(void) {
    FuseRadioSurveyProgressView* instance = malloc(sizeof(FuseRadioSurveyProgressView));

    instance->view = view_alloc();
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(FuseRadioSurveyProgressViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_survey_progress_view_draw_callback);

    return instance;
}

void fuse_radio_survey_progress_view_free(FuseRadioSurveyProgressView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_survey_progress_view_get_view(FuseRadioSurveyProgressView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_survey_progress_view_set_snapshot(
    FuseRadioSurveyProgressView* instance,
    const FuseRadioSurveyProgressSnapshot* snapshot) {
    furi_assert(instance);
    furi_assert(snapshot);
    with_view_model(
        instance->view,
        FuseRadioSurveyProgressViewModel * model,
        { model->snapshot = *snapshot; },
        true);
}

void fuse_radio_survey_progress_view_advance_animation(FuseRadioSurveyProgressView* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FuseRadioSurveyProgressViewModel * model,
        { model->snapshot.animation_frame++; },
        true);
}