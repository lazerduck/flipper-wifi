#include "fuse_radio_discover_result_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FuseRadioDiscoverResultView {
    View* view;
    FuseRadioDiscoverResultViewCallback callback;
    void* context;
};

typedef struct {
    FuseRadioDiscoverResults results;
    uint8_t page_index;
} FuseRadioDiscoverResultViewModel;

static void fuse_radio_discover_result_view_draw_badge(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    const char* text) {
    canvas_draw_box(canvas, x, y, 24, 11);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x + 2, y + 9, text);
    canvas_set_color(canvas, ColorBlack);
}

static const char* fuse_radio_discover_result_view_source_label(FuseRadioDiscoverNameSource source) {
    switch(source) {
    case FuseRadioDiscoverNameSourceMdns:
        return "mDNS";
    case FuseRadioDiscoverNameSourceReverseDns:
        return "DNS";
    case FuseRadioDiscoverNameSourceNone:
    default:
        return "IP";
    }
}

static void fuse_radio_discover_result_view_draw_overview(
    Canvas* canvas,
    const FuseRadioDiscoverResults* results) {
    char line[24];

    canvas_draw_rframe(canvas, 4, 17, 120, 38, 2);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 28, results->subnet[0] ? results->subnet : "Subnet unknown");
    canvas_set_font(canvas, FontSecondary);

    snprintf(line, sizeof(line), "Found %u", (unsigned)results->found_count);
    canvas_draw_str(canvas, 8, 39, line);
    snprintf(line, sizeof(line), "Scanned %u", (unsigned)results->scanned_count);
    canvas_draw_str(canvas, 70, 39, line);

    snprintf(line, sizeof(line), "%lums", (unsigned long)results->duration_ms);
    canvas_draw_str(canvas, 8, 50, line);
    if(results->truncated_count > 0U) {
        snprintf(line, sizeof(line), "+%u hidden", (unsigned)results->truncated_count);
        canvas_draw_str(canvas, 70, 50, line);
    } else if(results->self_ip[0]) {
        snprintf(line, sizeof(line), "Self %s", results->self_ip);
        canvas_draw_str(canvas, 54, 50, line);
    }
}

static void fuse_radio_discover_result_view_draw_host(
    Canvas* canvas,
    const FuseRadioDiscoverHost* host) {
    char line[28];

    canvas_draw_frame(canvas, 3, 17, 122, 38);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 28, host->ip);
    fuse_radio_discover_result_view_draw_badge(
        canvas, 98, 19, fuse_radio_discover_result_view_source_label(host->name_source));

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 8, 39, host->has_name ? host->name : "No hostname");
    snprintf(line, sizeof(line), "RTT %ums", (unsigned)host->rtt_ms);
    canvas_draw_str(canvas, 8, 50, line);
}

static void fuse_radio_discover_result_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioDiscoverResultViewModel* view_model = model;
    char header[22];
    uint8_t page_count = (uint8_t)(view_model->results.count + 1U);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(view_model->results.has_error) {
        canvas_draw_str(canvas, 4, 10, "Discover error");
        canvas_draw_line(canvas, 0, 13, 127, 13);
        elements_multiline_text_aligned(
            canvas, 64, 32, AlignCenter, AlignCenter, view_model->results.error);
        elements_button_center(canvas, "Retry");
        return;
    }

    snprintf(
        header,
        sizeof(header),
        "Hosts %u/%u",
        (unsigned)(view_model->page_index + 1U),
        (unsigned)page_count);
    canvas_draw_str(canvas, 4, 10, header);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(view_model->results.count == 0U) {
        elements_multiline_text_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "No active hosts found");
        elements_button_center(canvas, "Retry");
        return;
    }

    if(view_model->page_index == 0U) {
        fuse_radio_discover_result_view_draw_overview(canvas, &view_model->results);
    } else {
        fuse_radio_discover_result_view_draw_host(
            canvas, &view_model->results.hosts[view_model->page_index - 1U]);
    }

    elements_button_center(canvas, "Retry");
}

static bool fuse_radio_discover_result_view_input_callback(InputEvent* event, void* context) {
    FuseRadioDiscoverResultView* instance = context;
    bool consumed = false;

    if(event->type != InputTypeShort) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioDiscoverResultViewModel * model,
        {
            const uint8_t page_count =
                model->results.has_error ? 1U : (uint8_t)(model->results.count + 1U);

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
        instance->callback(FuseRadioDiscoverResultViewActionRepeat, instance->context);
    }

    return consumed;
}

FuseRadioDiscoverResultView* fuse_radio_discover_result_view_alloc(void) {
    FuseRadioDiscoverResultView* instance = malloc(sizeof(FuseRadioDiscoverResultView));

    instance->view = view_alloc();
    view_allocate_model(
        instance->view,
        ViewModelTypeLocking,
        sizeof(FuseRadioDiscoverResultViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_discover_result_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_discover_result_view_input_callback);

    return instance;
}

void fuse_radio_discover_result_view_free(FuseRadioDiscoverResultView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_discover_result_view_get_view(FuseRadioDiscoverResultView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_discover_result_view_set_callback(
    FuseRadioDiscoverResultView* instance,
    FuseRadioDiscoverResultViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_discover_result_view_set_data(
    FuseRadioDiscoverResultView* instance,
    const FuseRadioDiscoverResults* results) {
    furi_assert(instance);
    furi_assert(results);

    with_view_model(
        instance->view,
        FuseRadioDiscoverResultViewModel * model,
        {
            model->results = *results;
            if(model->results.has_error || model->results.count == 0U) {
                model->page_index = 0U;
            } else if(model->page_index > model->results.count) {
                model->page_index = model->results.count;
            }
        },
        true);
}