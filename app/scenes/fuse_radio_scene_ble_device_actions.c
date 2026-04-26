#include "../fuse_radio_app_i.h"

enum FuseRadioBleDeviceActionIndex {
    FuseRadioBleDeviceActionIndexInfo,
    FuseRadioBleDeviceActionIndexSaveToggle,
    FuseRadioBleDeviceActionIndexScanNearby,
    FuseRadioBleDeviceActionIndexGattInspect,
};

static void fuse_radio_scene_ble_device_actions_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_ble_device_actions_on_enter(void* context) {
    FuseRadioApp* app = context;

    submenu_set_header(
        app->submenu,
        app->ble_selection.device.has_name ? app->ble_selection.device.name : "BLE Device");
    submenu_add_item(
        app->submenu,
        "Info",
        FuseRadioBleDeviceActionIndexInfo,
        fuse_radio_scene_ble_device_actions_callback,
        app);
    submenu_add_item(
        app->submenu,
        app->ble_selection.is_saved ? "Remove Saved" : "Save Device",
        FuseRadioBleDeviceActionIndexSaveToggle,
        fuse_radio_scene_ble_device_actions_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Scan Nearby",
        FuseRadioBleDeviceActionIndexScanNearby,
        fuse_radio_scene_ble_device_actions_callback,
        app);

    if(app->ble_selection.device.connectable) {
        submenu_add_item(
            app->submenu,
            "Inspect (GATT)",
            FuseRadioBleDeviceActionIndexGattInspect,
            fuse_radio_scene_ble_device_actions_callback,
            app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_ble_device_actions_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case FuseRadioBleDeviceActionIndexInfo:
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleDeviceInfo);
            return true;
        case FuseRadioBleDeviceActionIndexSaveToggle:
            if(app->ble_selection.is_saved) {
                fuse_radio_app_remove_selected_ble_device(app);
            } else {
                fuse_radio_app_save_selected_ble_device(app);
            }
            scene_manager_previous_scene(app->scene_manager);
            return true;
        case FuseRadioBleDeviceActionIndexScanNearby:
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleScanPreset);
            return true;
        case FuseRadioBleDeviceActionIndexGattInspect:
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleGattProgress);
            return true;
        default:
            return false;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_device_actions_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}