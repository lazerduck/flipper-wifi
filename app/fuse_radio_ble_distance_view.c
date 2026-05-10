#include "fuse_radio_ble_distance_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Graph geometry (all in pixels) ------------------------------------------ */
/* Frame drawn at (0, 12, 128, 30) → outer rect y=12..41                       */
/* Inner drawing area: x=1..126, y=13..40                                       */
#define GRAPH_FRAME_X  0U
#define GRAPH_FRAME_Y  12U
#define GRAPH_FRAME_W  128U
#define GRAPH_FRAME_H  30U

#define GRAPH_X        1U   /* inner left edge */
#define GRAPH_Y_TOP    13U  /* inner top edge  */
#define GRAPH_W        126U /* inner width     */
#define GRAPH_H        28U  /* inner height    */

/* Bar width in pixels — 2px gives up to 63 bars across the 126px inner width */
#define BAR_W          2U
#define MAX_BARS       (GRAPH_W / BAR_W)   /* 63 */

/* RSSI → bar height mapping                                                    */
#define RSSI_FLOOR  (-100)
#define RSSI_CEIL   (-30)
#define RSSI_RANGE  (RSSI_CEIL - RSSI_FLOOR)   /* 70 */

struct FuseRadioBleDistanceView {
    View*                          view;
    FuseRadioBleDistanceViewCallback callback;
    void*                          context;
};

typedef struct {
    FuseRadioBleDistanceSnapshot snapshot;
} FuseRadioBleDistanceViewModel;

/* --------------------------------------------------------------------------- */

static void fuse_radio_ble_distance_view_draw_callback(Canvas* canvas, void* model) {
    const FuseRadioBleDistanceViewModel* m = model;
    const FuseRadioBleDistanceSnapshot*  s = &m->snapshot;

    char rssi_buf[16];
    char status_buf[48];
    char trend[10];

    canvas_clear(canvas);

    /* ---- Header row ---- */
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 8, s->header);

    canvas_set_font(canvas, FontPrimary);
    if(s->seen) {
        snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm", (int)s->rssi);
    } else {
        snprintf(rssi_buf, sizeof(rssi_buf), "not seen");
    }
    canvas_draw_str_aligned(canvas, 127, 9, AlignRight, AlignBottom, rssi_buf);

    /* ---- Separator ---- */
    canvas_draw_line(canvas, 0, 11, 127, 11);

    /* ---- Bar graph frame ---- */
    canvas_draw_frame(canvas, GRAPH_FRAME_X, GRAPH_FRAME_Y, GRAPH_FRAME_W, GRAPH_FRAME_H);

    /* ---- Bars (most recent = rightmost) ---- */
    const uint8_t draw_count =
        (s->history_count < MAX_BARS) ? s->history_count : (uint8_t)MAX_BARS;

    for(uint8_t i = 0U; i < draw_count; i++) {
        /* i=0 → most recent entry (rightmost bar on screen) */
        const uint8_t idx = (uint8_t)(
            (s->history_head + FUSE_RADIO_BLE_DISTANCE_VIEW_HISTORY - 1U - i) %
            FUSE_RADIO_BLE_DISTANCE_VIEW_HISTORY);

        const int8_t  rv    = s->history[idx];
        const uint8_t bar_x = (uint8_t)(GRAPH_X + GRAPH_W - BAR_W - (i * BAR_W));

        if(rv <= -127) {
            /* Not-seen: two-pixel dot at graph mid-height */
            const uint8_t dot_y = (uint8_t)(GRAPH_Y_TOP + GRAPH_H / 2U);
            canvas_draw_dot(canvas, bar_x,     dot_y);
            canvas_draw_dot(canvas, bar_x + 1U, dot_y);
        } else {
            /* Map RSSI value to bar height clamped to [RSSI_FLOOR, RSSI_CEIL] */
            int16_t clamped = rv;
            if(clamped < RSSI_FLOOR) clamped = RSSI_FLOOR;
            if(clamped > RSSI_CEIL)  clamped = RSSI_CEIL;

            const uint8_t bar_h =
                (uint8_t)((uint16_t)((clamped - RSSI_FLOOR) * (int16_t)GRAPH_H) /
                          (uint16_t)RSSI_RANGE);
            const uint8_t effective_h = (bar_h == 0U) ? 1U : bar_h;

            canvas_draw_box(
                canvas,
                bar_x,
                (uint8_t)(GRAPH_Y_TOP + GRAPH_H - effective_h),
                BAR_W,
                effective_h);
        }
    }

    /* ---- Status line ---- */
    /* Convert trend to lowercase */
    size_t j;
    for(j = 0U; j < sizeof(trend) - 1U && s->trend[j] != '\0'; j++) {
        trend[j] = (char)tolower((uint8_t)s->trend[j]);
    }
    trend[j] = '\0';

    canvas_set_font(canvas, FontSecondary);
    if(s->active) {
        snprintf(status_buf, sizeof(status_buf), "%s  n=%u", trend, (unsigned)s->samples);
    } else {
        snprintf(status_buf, sizeof(status_buf), "stopped  n=%u", (unsigned)s->samples);
    }
    canvas_draw_str(canvas, 0, 50, status_buf);

    /* ---- Buttons ---- */
    elements_button_left(canvas, "Back");
    if(s->stop_pending) {
        elements_button_center(canvas, "Wait");
    } else if(s->active) {
        elements_button_center(canvas, "Stop");
    } else {
        elements_button_center(canvas, "Start");
    }
}

static bool fuse_radio_ble_distance_view_input_callback(InputEvent* event, void* context) {
    FuseRadioBleDistanceView* instance = context;

    if(event->type != InputTypeShort) {
        return false;
    }

    if(event->key == InputKeyOk && instance->callback) {
        instance->callback(FuseRadioBleDistanceViewActionStop, instance->context);
        return true;
    }

    if(event->key == InputKeyLeft && instance->callback) {
        instance->callback(FuseRadioBleDistanceViewActionBack, instance->context);
        return true;
    }

    if(event->key == InputKeyBack && instance->callback) {
        instance->callback(FuseRadioBleDistanceViewActionBack, instance->context);
        return true;
    }

    return false;
}

/* --------------------------------------------------------------------------- */

FuseRadioBleDistanceView* fuse_radio_ble_distance_view_alloc(void) {
    FuseRadioBleDistanceView* instance = malloc(sizeof(FuseRadioBleDistanceView));

    instance->view = view_alloc();
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(FuseRadioBleDistanceViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_ble_distance_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_ble_distance_view_input_callback);

    return instance;
}

void fuse_radio_ble_distance_view_free(FuseRadioBleDistanceView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_ble_distance_view_get_view(FuseRadioBleDistanceView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_ble_distance_view_set_callback(
    FuseRadioBleDistanceView*        instance,
    FuseRadioBleDistanceViewCallback callback,
    void*                            context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context  = context;
}

void fuse_radio_ble_distance_view_set_snapshot(
    FuseRadioBleDistanceView*          instance,
    const FuseRadioBleDistanceSnapshot* snapshot) {
    furi_assert(instance);
    furi_assert(snapshot);

    with_view_model(
        instance->view,
        FuseRadioBleDistanceViewModel * m,
        { m->snapshot = *snapshot; },
        true);
}
