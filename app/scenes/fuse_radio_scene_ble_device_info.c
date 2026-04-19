#include "../fuse_radio_app_i.h"

void fuse_radio_scene_ble_device_info_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_app_refresh_ble_info_widget(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_ble_device_info_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_device_info_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
}