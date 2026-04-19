#include "../fuse_radio_app_i.h"

void fuse_radio_scene_ble_saved_devices_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_app_refresh_saved_ble_view(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewBleScan);
}

bool fuse_radio_scene_ble_saved_devices_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == FuseRadioCustomEventBleSelectDevice) {
        if(fuse_radio_app_select_saved_ble_device(app)) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleDeviceActions);
        }
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_saved_devices_on_exit(void* context) {
    FuseRadioApp* app = context;
    UNUSED(app);
}