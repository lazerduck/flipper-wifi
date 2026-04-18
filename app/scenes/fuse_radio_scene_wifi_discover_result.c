#include "../fuse_radio_app_i.h"

void fuse_radio_scene_wifi_discover_result_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_app_start_wifi_discover(app);
    fuse_radio_app_refresh_discover_widget(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_wifi_discover_result_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(
        event.type == SceneManagerEventTypeCustom &&
        event.event == FuseRadioCustomEventWifiDiscoverRefresh) {
        fuse_radio_app_start_wifi_discover(app);
        fuse_radio_app_refresh_discover_widget(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_discover_result_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
}
