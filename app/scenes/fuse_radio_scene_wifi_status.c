#include "../fuse_radio_app_i.h"

static bool fuse_radio_scene_wifi_status_return_to_menu(FuseRadioApp* app) {
    return scene_manager_search_and_switch_to_previous_scene(
        app->scene_manager, FuseRadioSceneWifiMenu);
}

void fuse_radio_scene_wifi_status_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_app_request_wifi_status(app);
    fuse_radio_app_refresh_wifi_info_widget(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_wifi_status_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiStatusRefresh) {
        fuse_radio_app_request_wifi_status(app);
        fuse_radio_app_refresh_wifi_info_widget(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiStatusMenu) {
        return fuse_radio_scene_wifi_status_return_to_menu(app);
    }

    if(event.type == SceneManagerEventTypeBack) {
        return fuse_radio_scene_wifi_status_return_to_menu(app);
    }

    return false;
}

void fuse_radio_scene_wifi_status_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
}
