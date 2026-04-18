#include "../fuse_radio_app_i.h"

void fuse_radio_scene_wifi_discover_progress_on_enter(void* context) {
    FuseRadioApp* app = context;

    if(!fuse_radio_app_start_wifi_discover(app)) {
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiDiscoverResult);
        return;
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewDiscoverProgress);
}

bool fuse_radio_scene_wifi_discover_progress_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiDiscoverDone) {
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiDiscoverResult);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiDiscoverFailed) {
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiDiscoverResult);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return true;
    }

    return false;
}

void fuse_radio_scene_wifi_discover_progress_on_exit(void* context) {
    FuseRadioApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}