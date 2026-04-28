#include "fuse_radio_startup_view.h"
#include "fuse_radio_icons.h"

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

static const Icon* fuse_radio_startup_view_get_splash_icon(uint8_t frame) {
    const uint8_t image_index = (uint8_t)((frame / 3U) % 3U);

    switch(image_index) {
    case 1U:
        return &I_splash2;
    case 2U:
        return &I_splash3;
    default:
        return &I_splash1;
    }
}

static void fuse_radio_startup_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioStartupViewModel* view_model = model;
    const bool show_action = view_model->state != FuseRadioStartupStateBooting;

    canvas_clear(canvas);
    canvas_draw_icon(canvas, 0, 0, fuse_radio_startup_view_get_splash_icon(view_model->animation_frame));

    canvas_draw_rframe(canvas, 79, 45, 46, 16, 2);
    if(view_model->state == FuseRadioStartupStateError) {
        canvas_set_font(canvas, FontPrimary);
    } else {
        canvas_set_font(canvas, FontSecondary);
    }
    elements_multiline_text_aligned(
        canvas,
        102,
        53,
        AlignCenter,
        AlignCenter,
        fuse_radio_startup_view_state_label(view_model->state));

    if(view_model->state == FuseRadioStartupStateBooting) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 82, 42, fuse_radio_startup_view_headline(view_model->state));
    }

    if(show_action) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 83, 42, "OK");
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
