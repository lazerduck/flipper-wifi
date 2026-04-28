#include "fuse_radio_startup_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdlib.h>

struct FuseRadioStartupView {
    View* view;
    FuseRadioStartupViewCallback callback;
    void* context;
};

typedef struct {
    FuseRadioStartupState state;
    uint8_t animation_frame;
} FuseRadioStartupViewModel;

static const char* fuse_radio_startup_view_state_label(FuseRadioStartupState state) {
    if(state == FuseRadioStartupStateError) {
        return "Retry";
    }

    if(state == FuseRadioStartupStateReady) {
        return "Continue";
    }

    return "Starting";
}

static const char* fuse_radio_startup_view_headline(FuseRadioStartupState state) {
    if(state == FuseRadioStartupStateError) {
        return "Module not ready";
    }

    if(state == FuseRadioStartupStateReady) {
        return "Link established";
    }

    return "Boot sequence";
}

static void fuse_radio_startup_view_draw_backdrop(Canvas* canvas, uint8_t frame) {
    const uint8_t phase = (uint8_t)(frame % 8U);
    const uint8_t sweep_x = (uint8_t)(12U + phase * 13U);

    canvas_draw_frame(canvas, 0, 0, 128, 64);
    canvas_draw_rframe(canvas, 3, 3, 122, 58, 2);
    canvas_draw_line(canvas, 8, 17, 120, 17);
    canvas_draw_line(canvas, 8, 37, 120, 37);

    for(uint8_t x = 10U; x < 120U; x += 10U) {
        canvas_draw_dot(canvas, x, 14);
        if(((x / 10U) + phase) % 3U == 0U) {
            canvas_draw_dot(canvas, x, 39);
        }
    }

    canvas_draw_rbox(canvas, 10, 43, 108, 5, 2);
    canvas_draw_box(canvas, sweep_x, 44, 14, 3);

    canvas_draw_frame(canvas, 12, 20, 18, 14);
    canvas_draw_line(canvas, 14, 31, 20, 23);
    canvas_draw_line(canvas, 20, 23, 24, 26);
    canvas_draw_line(canvas, 24, 26, 27, 22);

    if((phase % 2U) == 0U) {
        canvas_draw_box(canvas, 112, 8, 8, 4);
    } else {
        canvas_draw_frame(canvas, 112, 8, 8, 4);
    }
}

static void fuse_radio_startup_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioStartupViewModel* view_model = model;
    const bool show_action = view_model->state != FuseRadioStartupStateBooting;

    canvas_clear(canvas);
    fuse_radio_startup_view_draw_backdrop(canvas, view_model->animation_frame);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 36, 27, "FUSE RADIO");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 36, 35, fuse_radio_startup_view_headline(view_model->state));

    canvas_draw_rframe(canvas, 34, 43, 60, 16, 2);
    if(view_model->state == FuseRadioStartupStateError) {
        canvas_set_font(canvas, FontPrimary);
    } else {
        canvas_set_font(canvas, FontSecondary);
    }
    elements_multiline_text_aligned(
        canvas,
        64,
        52,
        AlignCenter,
        AlignCenter,
        fuse_radio_startup_view_state_label(view_model->state));

    if(show_action) {
        elements_button_center(
            canvas,
            view_model->state == FuseRadioStartupStateError ? "Retry" : "Enter");
    }
}

static bool fuse_radio_startup_view_input_callback(InputEvent* event, void* context) {
    FuseRadioStartupView* instance = context;
    bool can_act = false;

    if(event->type != InputTypeShort || event->key != InputKeyOk) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioStartupViewModel * model,
        { can_act = model->state != FuseRadioStartupStateBooting; },
        false);

    if(can_act && instance->callback) {
        instance->callback(instance->context);
        return true;
    }

    return false;
}

FuseRadioStartupView* fuse_radio_startup_view_alloc(void) {
    FuseRadioStartupView* instance = malloc(sizeof(FuseRadioStartupView));

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FuseRadioStartupViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_startup_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_startup_view_input_callback);

    fuse_radio_startup_view_set_state(instance, FuseRadioStartupStateBooting);

    return instance;
}

void fuse_radio_startup_view_free(FuseRadioStartupView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_startup_view_get_view(FuseRadioStartupView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_startup_view_set_callback(
    FuseRadioStartupView* instance,
    FuseRadioStartupViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_startup_view_set_state(
    FuseRadioStartupView* instance,
    FuseRadioStartupState state) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FuseRadioStartupViewModel * model,
        {
            model->state = state;
            if(state != FuseRadioStartupStateBooting) {
                model->animation_frame = 0U;
            }
        },
        true);
}

void fuse_radio_startup_view_advance_animation(FuseRadioStartupView* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        FuseRadioStartupViewModel * model,
        {
            if(model->state == FuseRadioStartupStateBooting) {
                model->animation_frame++;
            }
        },
        true);
}
