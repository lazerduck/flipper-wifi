#include "../fuse_radio_app_i.h"

enum FuseRadioModeSelectIndex {
    FuseRadioModeSelectIndexSwitchWifi,
    FuseRadioModeSelectIndexSwitchZigbee,
};

static void fuse_radio_scene_mode_select_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_mode_select_on_enter(void* context) {
    FuseRadioApp* app = context;

    submenu_reset(app->submenu);
    submenu_add_item(
        app->submenu,
        "Switch To WIFI",
        FuseRadioModeSelectIndexSwitchWifi,
        fuse_radio_scene_mode_select_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Switch To ZIGBEE",
        FuseRadioModeSelectIndexSwitchZigbee,
        fuse_radio_scene_mode_select_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_mode_select_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FuseRadioModeSelectIndexSwitchWifi) {
            if(fuse_radio_app_start_system_mode_set_wifi(app)) {
                scene_manager_next_scene(app->scene_manager, FuseRadioSceneStatus);
            }
            return true;
        }

        if(event.event == FuseRadioModeSelectIndexSwitchZigbee) {
            if(fuse_radio_app_start_system_mode_set_zigbee(app)) {
                scene_manager_next_scene(app->scene_manager, FuseRadioSceneStatus);
            }
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_mode_select_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}
