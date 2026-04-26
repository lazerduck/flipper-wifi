#include "../fuse_radio_app_i.h"

enum FuseRadioBleScanPresetIndex {
    FuseRadioBleScanPresetIndexNormal,
    FuseRadioBleScanPresetIndexDeep,
};

static void fuse_radio_scene_ble_scan_preset_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_ble_scan_preset_on_enter(void* context) {
    FuseRadioApp* app = context;
    const FuseRadioBleScanMode mode = fuse_radio_app_get_ble_scan_mode(app);

    submenu_set_header(app->submenu, "BLE Scan Type");
    submenu_add_item(
        app->submenu,
        "Normal 5s",
        FuseRadioBleScanPresetIndexNormal,
        fuse_radio_scene_ble_scan_preset_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Deep 30s",
        FuseRadioBleScanPresetIndexDeep,
        fuse_radio_scene_ble_scan_preset_callback,
        app);
    submenu_set_selected_item(
        app->submenu,
        mode == FuseRadioBleScanModeDeep ? FuseRadioBleScanPresetIndexDeep :
                                           FuseRadioBleScanPresetIndexNormal);

    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_ble_scan_preset_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FuseRadioBleScanPresetIndexDeep) {
            fuse_radio_app_set_ble_scan_mode(app, FuseRadioBleScanModeDeep);
        } else {
            fuse_radio_app_set_ble_scan_mode(app, FuseRadioBleScanModeNormal);
        }

        fuse_radio_app_start_ble_scan(app);
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleScan);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_scan_preset_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}
