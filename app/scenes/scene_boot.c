#include "../app.h"
#include <string.h>

typedef enum {
    BootEventDelayDone,   /* ESP init delay elapsed → time to send PING      */
    BootEventPingTimeout, /* no PONG within timeout → retry or show failure  */
    BootEventPong,        /* PONG received → go to main menu                 */
    BootEventRetry,       /* user pressed Retry on the failure dialog        */
} BootCustomEvent;

/* ── Timer callback ──────────────────────────────────────────────────────── */

static void boot_timer_cb(void* ctx) {
    App* app = ctx;
    if(app->boot_state == BootStateWaitEsp) {
        view_dispatcher_send_custom_event(app->view_dispatcher, BootEventDelayDone);
    } else if(app->boot_state == BootStateWaitPong) {
        view_dispatcher_send_custom_event(app->view_dispatcher, BootEventPingTimeout);
    }
}

/* ── UART line callback (runs in uart worker thread) ─────────────────────── */

static void boot_uart_line_cb(const char* line, void* ctx) {
    App* app = ctx;
    /* ESP responds to PING with: S msg="PONG" */
    if(line[0] == 'S' && strstr(line, "PONG")) {
        view_dispatcher_send_custom_event(app->view_dispatcher, BootEventPong);
    }
}

/* ── Retry button callback (DialogEx center button) ─────────────────────── */

static void boot_dialog_result_cb(DialogExResult result, void* ctx) {
    if(result == DialogExResultCenter) {
        App* app = ctx;
        view_dispatcher_send_custom_event(app->view_dispatcher, BootEventRetry);
    }
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void boot_show_connecting(App* app) {
    char header[32];
    snprintf(
        header,
        sizeof(header),
        "Connecting... %u/%u",
        (unsigned)(app->boot_retries + 1),
        (unsigned)APP_BOOT_MAX_RETRIES);

    popup_reset(app->popup);
    popup_set_header(app->popup, header, 64, 8, AlignCenter, AlignTop);
    popup_set_text(app->popup, "Waiting for ESP...", 64, 32, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewPopup);
}

static void boot_show_failure(App* app) {
    dialog_ex_reset(app->dialog_ex);
    dialog_ex_set_header(app->dialog_ex, "ESP not found", 64, 8, AlignCenter, AlignTop);
    dialog_ex_set_text(
        app->dialog_ex, "Check connection\nand try again.", 64, 28, AlignCenter, AlignTop);
    dialog_ex_set_center_button_text(app->dialog_ex, "Retry");
    dialog_ex_set_context(app->dialog_ex, app);
    dialog_ex_set_result_callback(app->dialog_ex, boot_dialog_result_cb);
    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewDialogEx);
}

static void boot_start_attempt(App* app) {
    boot_show_connecting(app);
    app->boot_state = BootStateWaitEsp;
    furi_timer_start(app->boot_timer, furi_ms_to_ticks(APP_ESP_INIT_DELAY_MS));
}

/* ── Scene callbacks ─────────────────────────────────────────────────────── */

void app_scene_boot_on_enter(void* ctx) {
    App* app = ctx;

    /* Guard against re-launch after a crash that left OTG on */
    if(!furi_hal_power_is_otg_enabled()) {
        furi_hal_power_enable_otg();
    }

    app->boot_retries = 0;
    app->boot_timer   = furi_timer_alloc(boot_timer_cb, FuriTimerTypeOnce, app);
    app->uart         = app_uart_alloc(boot_uart_line_cb, app);

    boot_start_attempt(app);
}

bool app_scene_boot_on_event(void* ctx, SceneManagerEvent event) {
    App* app      = ctx;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch((BootCustomEvent)event.event) {
        case BootEventDelayDone:
            /* ESP had time to start — send PING and arm the timeout */
            app_uart_send(app->uart, "PING\n");
            app->boot_state = BootStateWaitPong;
            furi_timer_start(app->boot_timer, furi_ms_to_ticks(APP_PING_TIMEOUT_MS));
            consumed = true;
            break;

        case BootEventPingTimeout:
            app->boot_retries++;
            if(app->boot_retries < APP_BOOT_MAX_RETRIES) {
                boot_start_attempt(app);
            } else {
                app->boot_state = BootStateFailed;
                boot_show_failure(app);
            }
            consumed = true;
            break;

        case BootEventPong:
            furi_timer_stop(app->boot_timer);
            scene_manager_next_scene(app->scene_manager, AppSceneMainMenu);
            consumed = true;
            break;

        case BootEventRetry:
            app->boot_retries = 0;
            boot_start_attempt(app);
            consumed = true;
            break;

        default:
            break;
        }
    }

    return consumed;
}

void app_scene_boot_on_exit(void* ctx) {
    App* app = ctx;
    furi_timer_stop(app->boot_timer);
    furi_timer_free(app->boot_timer);
    app->boot_timer = NULL;
    popup_reset(app->popup);
    dialog_ex_reset(app->dialog_ex);
}
