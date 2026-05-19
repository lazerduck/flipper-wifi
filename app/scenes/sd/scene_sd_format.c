#include "../../app.h"
#include <string.h>

typedef enum {
    SdFormatEventConfirm,
    SdFormatEventDone,
    SdFormatEventError,
} SdFormatEvent;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void sd_format_show_confirm1(App* app) {
    dialog_ex_reset(app->dialog_ex);
    dialog_ex_set_header(app->dialog_ex, "Format SD card?", 64, 8, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog_ex,
        "This will erase\nall data on the card.",
        64, 26, AlignCenter, AlignTop);
    dialog_ex_set_left_button_text(app->dialog_ex, "Cancel");
    dialog_ex_set_center_button_text(app->dialog_ex, "Continue");
    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewDialogEx);
}

static void sd_format_show_confirm2(App* app) {
    dialog_ex_reset(app->dialog_ex);
    dialog_ex_set_header(app->dialog_ex, "Are you sure?", 64, 8, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog_ex,
        "All files will be\npermanently deleted!",
        64, 26, AlignCenter, AlignTop);
    dialog_ex_set_left_button_text(app->dialog_ex, "Cancel");
    dialog_ex_set_center_button_text(app->dialog_ex, "Format");
    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewDialogEx);
}

static void sd_format_show_running(App* app) {
    popup_reset(app->popup);
    popup_set_header(app->popup, "SD Card", 64, 4, AlignCenter, AlignTop);
    popup_set_text(app->popup, "Formatting...", 64, 32, AlignCenter, AlignTop);
    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewPopup);
}

/* ── Dialog result callback ──────────────────────────────────────────────── */

static void sd_format_dialog_cb(DialogExResult result, void* ctx) {
    if(result == DialogExResultCenter) {
        App* app = ctx;
        view_dispatcher_send_custom_event(app->view_dispatcher, SdFormatEventConfirm);
    }
}

/* ── UART callback (worker thread context) ───────────────────────────────── */

static void sd_format_uart_cb(const char* line, void* ctx) {
    App* app = ctx;
    if(line[0] == 'S') {
        view_dispatcher_send_custom_event(app->view_dispatcher, SdFormatEventDone);
    } else if(line[0] == 'E') {
        view_dispatcher_send_custom_event(app->view_dispatcher, SdFormatEventError);
    }
}

/* ── Scene callbacks ─────────────────────────────────────────────────────── */

void app_scene_sd_format_on_enter(void* ctx) {
    App* app = ctx;
    app->sd_format_step = SdFormatConfirm1;
    dialog_ex_set_context(app->dialog_ex, app);
    dialog_ex_set_result_callback(app->dialog_ex, sd_format_dialog_cb);
    sd_format_show_confirm1(app);
}

bool app_scene_sd_format_on_event(void* ctx, SceneManagerEvent event) {
    App* app      = ctx;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        if(app->sd_format_step == SdFormatRunning) {
            /* Cannot interrupt a running format — swallow the back press */
            consumed = true;
        }
        /* else: allow scene manager to pop normally */
        return consumed;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        switch((SdFormatEvent)event.event) {
        case SdFormatEventConfirm:
            if(app->sd_format_step == SdFormatConfirm1) {
                app->sd_format_step = SdFormatConfirm2;
                sd_format_show_confirm2(app);
                consumed = true;
            } else if(app->sd_format_step == SdFormatConfirm2) {
                app->sd_format_step = SdFormatRunning;
                sd_format_show_running(app);
                app_uart_set_callback(app->uart, sd_format_uart_cb, app);
                app_uart_send(app->uart, "SD_FORMAT\n");
                consumed = true;
            }
            break;
        case SdFormatEventDone:
            app->sd_format_step = SdFormatDone;
            popup_set_text(app->popup, "Format complete!", 64, 32, AlignCenter, AlignTop);
            /* Leave popup visible — user presses Back to exit */
            app_uart_set_callback(app->uart, NULL, NULL);
            consumed = true;
            break;
        case SdFormatEventError:
            app->sd_format_step = SdFormatFailed;
            popup_set_text(app->popup, "Format failed!", 64, 32, AlignCenter, AlignTop);
            app_uart_set_callback(app->uart, NULL, NULL);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void app_scene_sd_format_on_exit(void* ctx) {
    App* app = ctx;
    app_uart_set_callback(app->uart, NULL, NULL);
    dialog_ex_reset(app->dialog_ex);
    popup_reset(app->popup);
    app->sd_format_step = SdFormatIdle;
}
