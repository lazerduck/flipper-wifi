#include "fuse_radio_watch_live_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>

struct FuseRadioWatchLiveView {
    View* view;
    FuseRadioWatchLiveViewCallback callback;
    void* context;
};

typedef struct {
    FuseRadioWatchLiveSnapshot snapshot;
} FuseRadioWatchLiveViewModel;

static void fuse_radio_watch_live_view_draw_tile(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    const char* label,
    const char* value) {
    canvas_draw_rframe(canvas, x, y, width, 20, 2);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x + 4, y + 9, label);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, x + 4, y + 18, value);
}

static void fuse_radio_watch_live_view_draw_signal(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    int16_t rssi) {
    uint8_t bars = 0U;

    if(rssi >= -55) {
        bars = 4U;
    } else if(rssi >= -67) {
        bars = 3U;
    } else if(rssi >= -78) {
        bars = 2U;
    } else if(rssi >= -90) {
        bars = 1U;
    }

    for(uint8_t index = 0; index < 4U; index++) {
        const uint8_t height = 3U + index * 2U;
        const uint8_t bar_x = x + index * 4U;
        const uint8_t bar_y = y - height;

        if(index < bars) {
            canvas_draw_box(canvas, bar_x, bar_y, 2, height);
        } else {
            canvas_draw_frame(canvas, bar_x, bar_y, 2, height);
        }
    }
}

static void fuse_radio_watch_live_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioWatchLiveViewModel* view_model = model;
    char header[24];
    char elapsed[12];
    char frames[12];
    char unique[8];
    char beacons[8];
    char rssi[12];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    snprintf(
        header,
        sizeof(header),
        view_model->snapshot.stop_pending ? "Watch ch%u wait" : "Watch ch%u live",
        (unsigned)view_model->snapshot.channel);
    canvas_draw_str(canvas, 4, 10, header);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    snprintf(elapsed, sizeof(elapsed), "%lus", (unsigned long)(view_model->snapshot.elapsed_ms / 1000UL));
    snprintf(frames, sizeof(frames), "%lu", (unsigned long)view_model->snapshot.total_frames);
    snprintf(unique, sizeof(unique), "%u", (unsigned)view_model->snapshot.unique_count);
    snprintf(beacons, sizeof(beacons), "%u", (unsigned)view_model->snapshot.beacon_count);
    if(view_model->snapshot.max_rssi <= -127) {
        snprintf(rssi, sizeof(rssi), "--");
    } else {
        snprintf(rssi, sizeof(rssi), "%d", view_model->snapshot.max_rssi);
    }

    fuse_radio_watch_live_view_draw_tile(canvas, 4, 14, 58, "Elapsed", elapsed);
    fuse_radio_watch_live_view_draw_tile(canvas, 66, 14, 58, "Frames", frames);
    fuse_radio_watch_live_view_draw_tile(canvas, 4, 33, 58, "Unique", unique);
    fuse_radio_watch_live_view_draw_tile(canvas, 66, 33, 58, "Beacons", beacons);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 89, 61, rssi);
    fuse_radio_watch_live_view_draw_signal(canvas, 109, 62, view_model->snapshot.max_rssi);

    if(view_model->snapshot.stop_pending) {
        elements_button_center(canvas, "Wait");
    } else {
        elements_button_center(canvas, "Stop");
    }
}

static bool fuse_radio_watch_live_view_input_callback(InputEvent* event, void* context) {
    FuseRadioWatchLiveView* instance = context;

    if(event->type == InputTypeShort && event->key == InputKeyOk && instance->callback) {
        instance->callback(instance->context);
        return true;
    }

    return false;
}

FuseRadioWatchLiveView* fuse_radio_watch_live_view_alloc(void) {
    FuseRadioWatchLiveView* instance = malloc(sizeof(FuseRadioWatchLiveView));

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FuseRadioWatchLiveViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_watch_live_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_watch_live_view_input_callback);

    return instance;
}

void fuse_radio_watch_live_view_free(FuseRadioWatchLiveView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_watch_live_view_get_view(FuseRadioWatchLiveView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_watch_live_view_set_callback(
    FuseRadioWatchLiveView* instance,
    FuseRadioWatchLiveViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_watch_live_view_set_snapshot(
    FuseRadioWatchLiveView* instance,
    const FuseRadioWatchLiveSnapshot* snapshot) {
    furi_assert(instance);
    furi_assert(snapshot);

    with_view_model(
        instance->view,
        FuseRadioWatchLiveViewModel * model,
        { model->snapshot = *snapshot; },
        true);
}