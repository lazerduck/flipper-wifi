#include "../fuse_radio_app_i.h"

void fuse_radio_scene_ble_scan_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_app_start_ble_scan(app);
    fuse_radio_app_refresh_ble_scan_view(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewBleScan);
}

bool fuse_radio_scene_ble_scan_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == FuseRadioCustomEventBleScanRefresh) {
        fuse_radio_app_start_ble_scan(app);
        fuse_radio_app_refresh_ble_scan_view(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom && event.event == FuseRadioCustomEventBleSaveSelected) {
        if(fuse_radio_app_save_selected_ble_device(app)) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleSavedDevices);
        }
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_scan_on_exit(void* context) {
    FuseRadioApp* app = context;
    UNUSED(app);
}