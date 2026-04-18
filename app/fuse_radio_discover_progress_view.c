#include "fuse_radio_discover_progress_view.h"

#include <gui/canvas.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FuseRadioDiscoverProgressView {
    View* view;
};

typedef struct {
    FuseRadioDiscoverProgressSnapshot snapshot;
} FuseRadioDiscoverProgressViewModel;

static const char* fuse_radio_discover_progress_view_status_text(uint8_t animation_frame) {
    switch(animation_frame % 3U) {
    case 1U:
        return "Pinging..";
    case 2U:
        return "Pinging...";
    default:
        return "Pinging.";
    }
}

static void fuse_radio_discover_progress_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioDiscoverProgressViewModel* view_model = model;
    char line[32];
    char line_b[32];
    char percent[8];
    uint8_t fill_width = (uint8_t)((108U * view_model->snapshot.progress_percent) / 100U);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 10, "Host discover");
    canvas_draw_line(canvas, 0, 13, 127, 13);

    canvas_set_font(canvas, FontSecondary);
    snprintf(
        line,
        sizeof(line),
        "%s",
        view_model->snapshot.subnet[0] ? view_model->snapshot.subnet : "Subnet unknown");
    canvas_draw_str(canvas, 8, 25, line);

    snprintf(percent, sizeof(percent), "%u%%", (unsigned)view_model->snapshot.progress_percent);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 95, 25, percent);

    canvas_draw_rframe(canvas, 9, 31, 110, 11, 2);
    if(fill_width > 0U) {
        canvas_draw_box(canvas, 10, 32, fill_width, 9);
    }

    canvas_set_font(canvas, FontSecondary);
    snprintf(
        line,
        sizeof(line),
        "Scanned %u  Found %u",
        (unsigned)view_model->snapshot.scanned_count,
        (unsigned)view_model->snapshot.found_count);
    canvas_draw_str(canvas, 8, 49, line);

    if(view_model->snapshot.current_ip[0]) {
        snprintf(line_b, sizeof(line_b), "Now %s", view_model->snapshot.current_ip);
    } else {
        snprintf(line_b, sizeof(line_b), "%s", fuse_radio_discover_progress_view_status_text(view_model->snapshot.animation_frame));
    }
    canvas_draw_str(canvas, 8, 60, line_b);
}

FuseRadioDiscoverProgressView* fuse_radio_discover_progress_view_alloc(void) {
    FuseRadioDiscoverProgressView* instance = malloc(sizeof(FuseRadioDiscoverProgressView));

    instance->view = view_alloc();
    view_allocate_model(
        instance->view,
        ViewModelTypeLocking,
        sizeof(FuseRadioDiscoverProgressViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_discover_progress_view_draw_callback);

    return instance;
}

void fuse_radio_discover_progress_view_free(FuseRadioDiscoverProgressView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_discover_progress_view_get_view(FuseRadioDiscoverProgressView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_discover_progress_view_set_snapshot(
    FuseRadioDiscoverProgressView* instance,
    const FuseRadioDiscoverProgressSnapshot* snapshot) {
    furi_assert(instance);
    furi_assert(snapshot);
    with_view_model(
        instance->view,
        FuseRadioDiscoverProgressViewModel * model,
        { model->snapshot = *snapshot; },
        true);
}

void fuse_radio_discover_progress_view_advance_animation(FuseRadioDiscoverProgressView* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FuseRadioDiscoverProgressViewModel * model,
        { model->snapshot.animation_frame++; },
        true);
}