#include "../fuse_radio_app_i.h"

enum FuseRadioBleMenuIndex {
    FuseRadioBleMenuIndexScan,
    FuseRadioBleMenuIndexSavedDevices,
};

static void fuse_radio_scene_ble_menu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_ble_menu_on_enter(void* context) {
    FuseRadioApp* app = context;
    uint32_t selected = scene_manager_get_scene_state(app->scene_manager, FuseRadioSceneBleMenu);

    submenu_add_item(
        app->submenu,
        "Scan Nearby",
        FuseRadioBleMenuIndexScan,
        fuse_radio_scene_ble_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Saved Devices",
        FuseRadioBleMenuIndexSavedDevices,
        fuse_radio_scene_ble_menu_callback,
        app);

    submenu_set_selected_item(app->submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_ble_menu_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, FuseRadioSceneBleMenu, event.event);

        if(event.event == FuseRadioBleMenuIndexScan) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleScanPreset);
        } else if(event.event == FuseRadioBleMenuIndexSavedDevices) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleSavedDevices);
        }

        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_menu_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}