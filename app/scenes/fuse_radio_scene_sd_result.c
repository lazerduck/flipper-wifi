#include "../fuse_radio_app_i.h"

#include <stdio.h>

void fuse_radio_scene_sd_result_on_enter(void* context) {
    FuseRadioApp* app = context;

    if(app->sd_action != FuseRadioSdActionFormat) {
        fuse_radio_app_start_sd_action(app, app->sd_action);
    }
    fuse_radio_app_refresh_sd_widget(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_sd_result_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == FuseRadioCustomEventSdRefresh) {
        app->sd_confirm_format = false;
        if(app->sd_action == FuseRadioSdActionDetail) {
            fuse_radio_app_start_sd_action(app, FuseRadioSdActionDetail);
        } else {
            fuse_radio_app_start_sd_action(app, app->sd_action);
        }
        fuse_radio_app_refresh_sd_widget(app);
        return true;
    }

    if(
        event.type == SceneManagerEventTypeCustom &&
        event.event == FuseRadioCustomEventSdFormatConfirm) {
        if(app->current_request != FuseRadioRequestNone) {
            return true;
        }

        if(!app->sd_confirm_format) {
            app->sd_confirm_format = true;
            snprintf(
                app->sd_info_text,
                sizeof(app->sd_info_text),
                "Format will initialize folders and may rewrite FAT.\n\nPress Confirm again to continue.");
            app->sd_dirty = true;
        } else {
            app->sd_confirm_format = false;
            fuse_radio_app_start_sd_action(app, FuseRadioSdActionFormat);
        }

        fuse_radio_app_refresh_sd_widget(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_sd_result_on_exit(void* context) {
    FuseRadioApp* app = context;
    app->sd_confirm_format = false;
    widget_reset(app->widget);
}
