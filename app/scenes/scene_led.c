#include "../app.h"
#include <stdio.h>

#define LED_STEP 16u
#define LED_MAX  240u

/* ── Layout constants (128×64 display) ──────────────────────────────────── */
#define LABEL_X   2
#define BAR_X     16
#define BAR_W     84
#define BAR_H     9
#define VAL_X     103

static const uint8_t ROW_BASELINE[] = {24, 38, 52};
static const char* const CHANNEL_LABELS[] = {"R", "G", "B"};

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static uint8_t* led_channel_ptr(App* app) {
    switch(app->led_channel) {
    case 0:  return &app->led_r;
    case 1:  return &app->led_g;
    default: return &app->led_b;
    }
}

static void led_send(App* app) {
    char cmd[48];
    uint8_t r = app->led_on ? app->led_r : 0;
    uint8_t g = app->led_on ? app->led_g : 0;
    uint8_t b = app->led_on ? app->led_b : 0;
    snprintf(cmd, sizeof(cmd), "LED_SET r=\"%u\" g=\"%u\" b=\"%u\"\n", r, g, b);
    app_uart_send(app->uart, cmd);
}

/* ── Draw callback ───────────────────────────────────────────────────────── */

static void led_draw_cb(Canvas* canvas, void* ctx) {
    App* app = *(App**)ctx;

    canvas_clear(canvas);

    /* Header */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, LABEL_X, 10, "LED");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 98, 10, app->led_on ? "[ON] " : "[OFF]");

    /* Divider */
    canvas_draw_line(canvas, 0, 12, 127, 12);

    /* Channel rows */
    const uint8_t values[3] = {app->led_r, app->led_g, app->led_b};

    for(uint8_t i = 0; i < 3; i++) {
        uint8_t bl = ROW_BASELINE[i]; /* text baseline   */
        uint8_t bt = bl - 8;          /* box/bar top     */

        canvas_set_font(canvas, FontSecondary);

        /* Highlight selected channel by inverting its label */
        if(app->led_channel == i) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, bt, 13, BAR_H);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, LABEL_X, bl, CHANNEL_LABELS[i]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, LABEL_X, bl, CHANNEL_LABELS[i]);
        }

        /* Bar outline */
        canvas_draw_frame(canvas, BAR_X, bt, BAR_W, BAR_H);

        /* Bar fill */
        if(values[i] > 0) {
            uint8_t fill = (uint8_t)((uint16_t)values[i] * (BAR_W - 2) / LED_MAX);
            if(fill > 0) canvas_draw_box(canvas, BAR_X + 1, bt + 1, fill, BAR_H - 2);
        }

        /* Numeric value */
        char val[4];
        snprintf(val, sizeof(val), "%3u", values[i]);
        canvas_draw_str(canvas, VAL_X, bl, val);
    }

    /* Hint */
    canvas_draw_str(canvas, 0, 63, "\x1b^v chan  <> val  OK on/off");
}

/* ── Input callback ──────────────────────────────────────────────────────── */

static bool led_input_cb(InputEvent* event, void* ctx) {
    App* app = ctx;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    bool consumed = true;
    bool send = false;

    switch(event->key) {
    case InputKeyUp:
        app->led_channel = (app->led_channel + 2) % 3;
        break;
    case InputKeyDown:
        app->led_channel = (app->led_channel + 1) % 3;
        break;
    case InputKeyRight: {
        uint8_t* v = led_channel_ptr(app);
        if(*v + LED_STEP <= LED_MAX) *v += LED_STEP;
        send = true;
        break;
    }
    case InputKeyLeft: {
        uint8_t* v = led_channel_ptr(app);
        if(*v >= LED_STEP) *v -= LED_STEP;
        send = true;
        break;
    }
    case InputKeyOk:
        app->led_on = !app->led_on;
        send = true;
        break;
    case InputKeyBack:
        consumed = false; /* let scene manager pop the scene */
        break;
    default:
        consumed = false;
        break;
    }

    if(send) led_send(app);

    /* Poke the model (no data change needed) so the view redraws */
    with_view_model(app->led_view, App** model, { (void)model; }, true);

    return consumed;
}

/* ── Scene callbacks ─────────────────────────────────────────────────────── */

void app_scene_led_on_enter(void* ctx) {
    App* app = ctx;

    /* Store App pointer in the view model so the draw callback can reach it */
    view_set_context(app->led_view, app);
    view_set_draw_callback(app->led_view, led_draw_cb);
    view_set_input_callback(app->led_view, led_input_cb);
    with_view_model(app->led_view, App** model, { *model = app; }, false);

    /* Push current values to the ESP on entry */
    led_send(app);

    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewLed);
}

bool app_scene_led_on_event(void* ctx, SceneManagerEvent event) {
    UNUSED(ctx);
    UNUSED(event);
    return false;
}

void app_scene_led_on_exit(void* ctx) {
    App* app = ctx;
    /* Clear the callbacks so the view is safe to reuse later */
    view_set_draw_callback(app->led_view, NULL);
    view_set_input_callback(app->led_view, NULL);
}
