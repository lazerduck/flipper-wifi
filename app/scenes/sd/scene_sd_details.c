#include "../../app.h"
#include "sd_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SdDetailsEventUpdated,
    SdDetailsEventError,
} SdDetailsEvent;

/* ── UART callback (worker thread context) ───────────────────────────────── */

static void sd_details_uart_cb(const char* line, void* ctx) {
    App* app = ctx;
    char val[32];

    if(line[0] == 'E') {
        view_dispatcher_send_custom_event(app->view_dispatcher, SdDetailsEventError);
        return;
    }
    if(line[0] != 'S') return; /* ignore DATA / END from stale streams */

    /* Parse S present="..." mounted="..." free_kb="..." total_kb="..." fs_type="..." */
    if(sd_parse_field(line, "present", val, sizeof(val)))
        app->sd_present = (strcmp(val, "true") == 0);
    if(sd_parse_field(line, "mounted", val, sizeof(val)))
        app->sd_mounted = (strcmp(val, "true") == 0);
    if(sd_parse_field(line, "free_kb", val, sizeof(val)))
        app->sd_free_kb = (uint32_t)atoi(val);
    if(sd_parse_field(line, "total_kb", val, sizeof(val)))
        app->sd_total_kb = (uint32_t)atoi(val);
    if(sd_parse_field(line, "fs_type", val, sizeof(val)))
        strncpy(app->sd_fs_type, val, sizeof(app->sd_fs_type) - 1);

    view_dispatcher_send_custom_event(app->view_dispatcher, SdDetailsEventUpdated);
}

/* ── Draw callback ───────────────────────────────────────────────────────── */

static void sd_details_draw_cb(Canvas* canvas, void* ctx) {
    App* app = *(App**)ctx;
    char line[52];

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "SD Card Details");
    canvas_draw_line(canvas, 0, 12, 127, 12);

    canvas_set_font(canvas, FontSecondary);

    if(!app->sd_present) {
        canvas_draw_str(canvas, 2, 28, "No card detected.");
        canvas_draw_str(canvas, 2, 42, "Insert card,");
        canvas_draw_str(canvas, 2, 52, "then press OK to remount.");
        canvas_draw_str(canvas, 80, 63, "OK=Remount");
        return;
    }

    /* Row 1: present + mounted */
    snprintf(
        line,
        sizeof(line),
        "Present: Yes   Mounted: %s",
        app->sd_mounted ? "Yes" : "No");
    canvas_draw_str(canvas, 2, 23, line);

    if(!app->sd_mounted) {
        canvas_draw_str(canvas, 2, 37, "Card not mounted.");
        canvas_draw_str(canvas, 80, 63, "OK=Remount");
        return;
    }

    /* Row 2: capacity bar */
    canvas_draw_str(canvas, 2, 35, "Used:");
    if(app->sd_total_kb > 0) {
        uint32_t used_kb  = app->sd_total_kb - app->sd_free_kb;
        uint8_t  pct      = (uint8_t)(used_kb * 100 / app->sd_total_kb);
        uint8_t  bar_fill = (uint8_t)((uint16_t)pct * 76 / 100);
        canvas_draw_frame(canvas, 30, 27, 78, 8);
        if(bar_fill > 0) canvas_draw_box(canvas, 31, 28, bar_fill, 6);
        snprintf(line, sizeof(line), "%u%%", pct);
        canvas_draw_str(canvas, 111, 35, line);
    }

    /* Row 3: free / total */
    uint32_t free_mb  = app->sd_free_kb  / 1024;
    uint32_t total_mb = app->sd_total_kb / 1024;
    snprintf(line, sizeof(line), "Free: %luMB  Total: %luMB",
             (unsigned long)free_mb, (unsigned long)total_mb);
    canvas_draw_str(canvas, 2, 47, line);

    /* Row 4: FS type + hint */
    snprintf(line, sizeof(line), "FS: %s", app->sd_fs_type);
    canvas_draw_str(canvas, 2, 59, line);
    canvas_draw_str(canvas, 80, 63, "OK=Remount");
}

/* ── Input callback ──────────────────────────────────────────────────────── */

static bool sd_details_input_cb(InputEvent* event, void* ctx) {
    App* app = ctx;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyOk) {
        app_uart_send(app->uart, "SD_REMOUNT\n");
        return true;
    }
    return false;
}

/* ── Scene callbacks ─────────────────────────────────────────────────────── */

void app_scene_sd_details_on_enter(void* ctx) {
    App* app = ctx;

    app_uart_set_callback(app->uart, sd_details_uart_cb, app);

    view_set_context(app->sd_details_view, app);
    view_set_draw_callback(app->sd_details_view, sd_details_draw_cb);
    view_set_input_callback(app->sd_details_view, sd_details_input_cb);
    with_view_model(app->sd_details_view, App** model, { *model = app; }, false);

    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewSdDetails);
    app_uart_send(app->uart, "SD_STATUS\n");
}

bool app_scene_sd_details_on_event(void* ctx, SceneManagerEvent event) {
    App* app = ctx;
    UNUSED(app);
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        /* Both success and error: just trigger a redraw with whatever state we have */
        with_view_model(app->sd_details_view, App** model, { (void)model; }, true);
        consumed = true;
    }
    return consumed;
}

void app_scene_sd_details_on_exit(void* ctx) {
    App* app = ctx;
    app_uart_set_callback(app->uart, NULL, NULL);
}
