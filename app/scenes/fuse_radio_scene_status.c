#include "../fuse_radio_app_i.h"

void fuse_radio_scene_status_on_enter(void* context) {
    FuseRadioApp* app = context;
    fuse_radio_app_refresh_status_widget(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_status_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FuseRadioCustomEventModuleDetected) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneMainMenu);
            return true;
        } else if(event.event == FuseRadioCustomEventRetryDetection) {
            fuse_radio_app_retry_session(app);
            fuse_radio_app_refresh_status_widget(app);
            return true;
        }
    }

    return false;
}

void fuse_radio_scene_status_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
}
