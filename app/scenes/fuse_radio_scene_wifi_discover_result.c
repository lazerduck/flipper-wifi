#include "../fuse_radio_app_i.h"

static bool fuse_radio_scene_wifi_discover_result_return_to_menu(FuseRadioApp* app) {
    return scene_manager_search_and_switch_to_previous_scene(
        app->scene_manager, FuseRadioSceneWifiConnectedMenu);
}

void fuse_radio_scene_wifi_discover_result_on_enter(void* context) {
    FuseRadioApp* app = context;
    fuse_radio_discover_result_view_set_data(app->discover_result_view, &app->discover_results);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewDiscoverResult);
}

bool fuse_radio_scene_wifi_discover_result_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(
        event.type == SceneManagerEventTypeCustom &&
        event.event == FuseRadioCustomEventWifiDiscoverRefresh) {
        fuse_radio_app_start_wifi_discover(app);
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, FuseRadioSceneWifiDiscoverProgress);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return fuse_radio_scene_wifi_discover_result_return_to_menu(app);
    }

    return false;
}

void fuse_radio_scene_wifi_discover_result_on_exit(void* context) {
    FuseRadioApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}
