#include "fuse_radio_watch_result_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FuseRadioWatchResultView {
    View* view;
    FuseRadioWatchResultViewCallback callback;
    void* context;
};

typedef struct {
    FuseRadioWatchSummary summary;
    FuseRadioWatchDevice devices[FUSE_RADIO_MAX_WATCH_DEVICES];
    uint8_t count;
    uint8_t selected_index;
} FuseRadioWatchResultViewModel;

static uint8_t fuse_radio_watch_result_signal_bars(int16_t rssi) {
    if(rssi >= -55) return 4U;
    if(rssi >= -67) return 3U;
    if(rssi >= -78) return 2U;
    if(rssi >= -90) return 1U;
    return 0U;
}

static void fuse_radio_watch_result_draw_signal(Canvas* canvas, int16_t rssi) {
    const uint8_t bars = fuse_radio_watch_result_signal_bars(rssi);
    const uint8_t base_x = 105U;
    const uint8_t base_y = 51U;

    for(uint8_t index = 0; index < 4U; index++) {
        const uint8_t height = 3U + index * 3U;
        const uint8_t x = (uint8_t)(base_x + index * 5U);
        const uint8_t y = (uint8_t)(base_y - height);

        if(index < bars) {
            canvas_draw_box(canvas, x, y, 3, height);
        } else {
            canvas_draw_frame(canvas, x, y, 3, height);
        }
    }
}

static void fuse_radio_watch_result_draw_ap_badge(Canvas* canvas, uint8_t x, uint8_t y) {
    canvas_draw_box(canvas, x, y, 14, 11);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x + 2, y + 9, "AP");
    canvas_set_color(canvas, ColorBlack);
}

static void fuse_radio_watch_result_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioWatchResultViewModel* view_model = model;
    char header[24];
    char frames_line[28];
    char detail_line[20];
    char rssi_line[12];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    snprintf(
        header,
        sizeof(header),
        "Watch ch%u %u/%u",
        (unsigned)view_model->summary.channel,
        (unsigned)(view_model->selected_index + 1U),
        (unsigned)view_model->count);
    canvas_draw_str(canvas, 4, 10, header);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(view_model->count == 0U) {
        elements_multiline_text_aligned(
            canvas, 64, 32, AlignCenter, AlignCenter, "No observed devices");
        elements_button_center(canvas, "Repeat");
        return;
    }

    const FuseRadioWatchDevice* device = &view_model->devices[view_model->selected_index];
    canvas_draw_frame(canvas, 3, 17, 122, 38);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 28, device->mac);
    if(device->likely_ap) {
        fuse_radio_watch_result_draw_ap_badge(canvas, 107, 19);
    }

    canvas_set_font(canvas, FontSecondary);
    snprintf(frames_line, sizeof(frames_line), "Frames %lu", (unsigned long)device->frames);
    snprintf(detail_line, sizeof(detail_line), "Beacons %u", (unsigned)device->beacons);
    if(device->max_rssi <= -127) {
        snprintf(rssi_line, sizeof(rssi_line), "--");
    } else {
        snprintf(rssi_line, sizeof(rssi_line), "%d", device->max_rssi);
    }

    canvas_draw_str(canvas, 8, 39, frames_line);
    canvas_draw_str(canvas, 8, 50, detail_line);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 81, 50, rssi_line);
    fuse_radio_watch_result_draw_signal(canvas, device->max_rssi);

    elements_button_center(canvas, "Repeat");
}

static bool fuse_radio_watch_result_view_input_callback(InputEvent* event, void* context) {
    FuseRadioWatchResultView* instance = context;
    bool consumed = false;

    if(event->type != InputTypeShort) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioWatchResultViewModel * model,
        {
            if(event->key == InputKeyUp && model->count > 0U) {
                if(model->selected_index > 0U) {
                    model->selected_index--;
                } else {
                    model->selected_index = model->count - 1U;
                }
                consumed = true;
            } else if(event->key == InputKeyDown && model->count > 0U) {
                model->selected_index = (uint8_t)((model->selected_index + 1U) % model->count);
                consumed = true;
            } else if(event->key == InputKeyOk) {
                consumed = true;
            }
        },
        consumed);

    if(consumed && event->key == InputKeyOk && instance->callback) {
        instance->callback(FuseRadioWatchResultViewActionRepeat, instance->context);
    }

    return consumed;
}

FuseRadioWatchResultView* fuse_radio_watch_result_view_alloc(void) {
    FuseRadioWatchResultView* instance = malloc(sizeof(FuseRadioWatchResultView));

    instance->view = view_alloc();
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(FuseRadioWatchResultViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_watch_result_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_watch_result_view_input_callback);

    return instance;
}

void fuse_radio_watch_result_view_free(FuseRadioWatchResultView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_watch_result_view_get_view(FuseRadioWatchResultView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_watch_result_view_set_callback(
    FuseRadioWatchResultView* instance,
    FuseRadioWatchResultViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_watch_result_view_set_data(
    FuseRadioWatchResultView* instance,
    const FuseRadioWatchSummary* summary,
    const FuseRadioWatchDevice* devices,
    uint8_t count) {
    furi_assert(instance);
    furi_assert(summary);

    with_view_model(
        instance->view,
        FuseRadioWatchResultViewModel * model,
        {
            model->summary = *summary;
            model->count = count > FUSE_RADIO_MAX_WATCH_DEVICES ? FUSE_RADIO_MAX_WATCH_DEVICES : count;
            if(devices != NULL && model->count > 0U) {
                memcpy(model->devices, devices, sizeof(model->devices[0]) * model->count);
            }
            if(model->count == 0U) {
                model->selected_index = 0U;
            } else if(model->selected_index >= model->count) {
                model->selected_index = model->count - 1U;
            }
        },
        true);
}