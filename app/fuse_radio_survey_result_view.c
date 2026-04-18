#include "fuse_radio_survey_result_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FuseRadioSurveyResultView {
    View* view;
    FuseRadioSurveyResultViewCallback callback;
    void* context;
};

typedef struct {
    FuseRadioSurveyResults results;
    uint8_t page_index;
} FuseRadioSurveyResultViewModel;

static uint8_t fuse_radio_survey_result_signal_bars(int16_t rssi) {
    if(rssi >= -55) return 4U;
    if(rssi >= -67) return 3U;
    if(rssi >= -78) return 2U;
    if(rssi >= -90) return 1U;
    return 0U;
}

static void fuse_radio_survey_result_draw_signal(Canvas* canvas, int16_t rssi) {
    const uint8_t bars = fuse_radio_survey_result_signal_bars(rssi);

    for(uint8_t index = 0; index < 4U; index++) {
        const uint8_t height = 3U + index * 2U;
        const uint8_t x = (uint8_t)(106U + index * 4U);
        const uint8_t y = (uint8_t)(50U - height);

        if(index < bars) {
            canvas_draw_box(canvas, x, y, 2, height);
        } else {
            canvas_draw_frame(canvas, x, y, 2, height);
        }
    }
}

static void fuse_radio_survey_result_draw_badge(Canvas* canvas, uint8_t x, uint8_t y, const char* text) {
    canvas_draw_box(canvas, x, y, 20, 11);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x + 2, y + 9, text);
    canvas_set_color(canvas, ColorBlack);
}

static void fuse_radio_survey_result_draw_overview(
    Canvas* canvas,
    const FuseRadioSurveyResults* results) {
    char best_line[16];
    char duration_line[16];
    char channels_line[16];
    char dwell_line[16];

    fuse_radio_survey_result_draw_badge(canvas, 8, 18, "BEST");
    snprintf(best_line, sizeof(best_line), "Ch %u", (unsigned)results->recommended_channel);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 33, 27, best_line);

    canvas_draw_rframe(canvas, 4, 32, 58, 18, 2);
    canvas_draw_rframe(canvas, 66, 32, 58, 18, 2);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 8, 40, "Channels");
    canvas_draw_str(canvas, 70, 40, "Duration");
    canvas_set_font(canvas, FontPrimary);
    snprintf(channels_line, sizeof(channels_line), "%u", (unsigned)results->result_count);
    snprintf(duration_line, sizeof(duration_line), "%lus", (unsigned long)(results->duration_ms / 1000UL));
    canvas_draw_str(canvas, 8, 48, channels_line);
    canvas_draw_str(canvas, 70, 48, duration_line);

    canvas_set_font(canvas, FontSecondary);
    snprintf(dwell_line, sizeof(dwell_line), "Dwell %ums", (unsigned)results->dwell_ms);
    canvas_draw_str(canvas, 8, 61, dwell_line);
}

static void fuse_radio_survey_result_draw_channel(
    Canvas* canvas,
    const FuseRadioSurveyResults* results,
    const FuseRadioSurveyChannelResult* result) {
    char line_a[20];
    char line_b[20];
    char rssi[10];
    char header[10];

    canvas_draw_frame(canvas, 3, 17, 122, 38);
    canvas_set_font(canvas, FontPrimary);
    snprintf(header, sizeof(header), "CH %u", (unsigned)result->channel);
    canvas_draw_str(canvas, 8, 28, header);
    if(result->channel == results->recommended_channel) {
        fuse_radio_survey_result_draw_badge(canvas, 99, 19, "BEST");
    }

    canvas_set_font(canvas, FontSecondary);
    snprintf(line_a, sizeof(line_a), "Frames %lu", (unsigned long)result->total_frames);
    snprintf(line_b, sizeof(line_b), "Unique %u", (unsigned)result->unique_count);
    canvas_draw_str(canvas, 8, 39, line_a);
    canvas_draw_str(canvas, 68, 39, line_b);

    snprintf(line_a, sizeof(line_a), "Beacons %u", (unsigned)result->beacon_frames);
    snprintf(line_b, sizeof(line_b), "Mgmt %lu", (unsigned long)result->management_frames);
    canvas_draw_str(canvas, 8, 50, line_a);
    canvas_draw_str(canvas, 68, 50, line_b);

    canvas_set_font(canvas, FontPrimary);
    if(result->max_rssi <= -127) {
        snprintf(rssi, sizeof(rssi), "--");
    } else {
        snprintf(rssi, sizeof(rssi), "%d", result->max_rssi);
    }
    canvas_draw_str(canvas, 85, 61, rssi);
    fuse_radio_survey_result_draw_signal(canvas, result->max_rssi);
}

static void fuse_radio_survey_result_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioSurveyResultViewModel* view_model = model;
    char header[22];
    uint8_t page_count = (uint8_t)(view_model->results.result_count + 1U);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    snprintf(
        header,
        sizeof(header),
        "Survey %u/%u",
        (unsigned)(view_model->page_index + 1U),
        (unsigned)page_count);
    canvas_draw_str(canvas, 4, 10, header);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(view_model->results.result_count == 0U) {
        elements_multiline_text_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "No survey data");
        elements_button_center(canvas, "Repeat");
        return;
    }

    if(view_model->page_index == 0U) {
        fuse_radio_survey_result_draw_overview(canvas, &view_model->results);
    } else {
        fuse_radio_survey_result_draw_channel(
            canvas,
            &view_model->results,
            &view_model->results.channels[view_model->page_index - 1U]);
    }

    elements_button_center(canvas, "Repeat");
}

static bool fuse_radio_survey_result_view_input_callback(InputEvent* event, void* context) {
    FuseRadioSurveyResultView* instance = context;
    bool consumed = false;

    if(event->type != InputTypeShort) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioSurveyResultViewModel * model,
        {
            uint8_t page_count = (uint8_t)(model->results.result_count + 1U);

            if(event->key == InputKeyUp && page_count > 0U) {
                if(model->page_index > 0U) {
                    model->page_index--;
                } else {
                    model->page_index = page_count - 1U;
                }
                consumed = true;
            } else if(event->key == InputKeyDown && page_count > 0U) {
                model->page_index = (uint8_t)((model->page_index + 1U) % page_count);
                consumed = true;
            } else if(event->key == InputKeyOk) {
                consumed = true;
            }
        },
        consumed);

    if(consumed && event->key == InputKeyOk && instance->callback) {
        instance->callback(FuseRadioSurveyResultViewActionRepeat, instance->context);
    }

    return consumed;
}

FuseRadioSurveyResultView* fuse_radio_survey_result_view_alloc(void) {
    FuseRadioSurveyResultView* instance = malloc(sizeof(FuseRadioSurveyResultView));

    instance->view = view_alloc();
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(FuseRadioSurveyResultViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_survey_result_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_survey_result_view_input_callback);

    return instance;
}

void fuse_radio_survey_result_view_free(FuseRadioSurveyResultView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_survey_result_view_get_view(FuseRadioSurveyResultView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_survey_result_view_set_callback(
    FuseRadioSurveyResultView* instance,
    FuseRadioSurveyResultViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_survey_result_view_set_data(
    FuseRadioSurveyResultView* instance,
    const FuseRadioSurveyResults* results) {
    furi_assert(instance);
    furi_assert(results);

    with_view_model(
        instance->view,
        FuseRadioSurveyResultViewModel * model,
        {
            model->results = *results;
            if(model->page_index > model->results.result_count) {
                model->page_index = 0U;
            }
        },
        true);
}