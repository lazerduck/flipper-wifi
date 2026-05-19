#include "../../app.h"
#include <string.h>
#include <stdio.h>

typedef enum {
    SdFileActionEventConfirm,
} SdFileActionEvent;

static void sd_file_action_dialog_cb(DialogExResult result, void* ctx) {
    if(result == DialogExResultCenter) {
        App* app = ctx;
        view_dispatcher_send_custom_event(app->view_dispatcher, SdFileActionEventConfirm);
    }
    /* Left / Back: the navigation callback pops the scene naturally */
}

void app_scene_sd_file_action_on_enter(void* ctx) {
    App* app = ctx;

    /* Extract just the filename from the full path for display */
    const char* slash = strrchr(app->sd_action_path, '/');
    const char* fname = (slash && slash[1]) ? slash + 1 : app->sd_action_path;

    dialog_ex_reset(app->dialog_ex);
    dialog_ex_set_header(app->dialog_ex, "Delete file?", 64, 8, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog_ex, fname, 64, 30, AlignCenter, AlignTop);
    dialog_ex_set_left_button_text(app->dialog_ex, "Cancel");
    dialog_ex_set_center_button_text(app->dialog_ex, "Delete");
    dialog_ex_set_context(app->dialog_ex, app);
    dialog_ex_set_result_callback(app->dialog_ex, sd_file_action_dialog_cb);

    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewDialogEx);
}

bool app_scene_sd_file_action_on_event(void* ctx, SceneManagerEvent event) {
    App* app      = ctx;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == SdFileActionEventConfirm) {
        char cmd[344];
        snprintf(cmd, sizeof(cmd), "SD_DELETE path=\"%s\"\n", app->sd_action_path);
        app_uart_send(app->uart, cmd);
        /* Pop back immediately — the explorer re-fetches on re-enter */
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }
    return consumed;
}

void app_scene_sd_file_action_on_exit(void* ctx) {
    App* app = ctx;
    dialog_ex_reset(app->dialog_ex);
}
