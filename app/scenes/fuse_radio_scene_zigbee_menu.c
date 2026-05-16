#include "../fuse_radio_app_i.h"

enum FuseRadioZigbeeMenuIndex {
    FuseRadioZigbeeMenuIndexStatus,
    FuseRadioZigbeeMenuIndexScan,
    FuseRadioZigbeeMenuIndexProfiles,
    FuseRadioZigbeeMenuIndexCreateProfile,
    FuseRadioZigbeeMenuIndexLeaveNetwork,
};

static void fuse_radio_scene_zigbee_menu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_zigbee_menu_on_enter(void* context) {
    FuseRadioApp* app = context;
    uint32_t selected = scene_manager_get_scene_state(app->scene_manager, FuseRadioSceneZigbeeMenu);

    submenu_add_item(
        app->submenu,
        "Status",
        FuseRadioZigbeeMenuIndexStatus,
        fuse_radio_scene_zigbee_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Scan Networks",
        FuseRadioZigbeeMenuIndexScan,
        fuse_radio_scene_zigbee_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Profiles",
        FuseRadioZigbeeMenuIndexProfiles,
        fuse_radio_scene_zigbee_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Create Profile",
        FuseRadioZigbeeMenuIndexCreateProfile,
        fuse_radio_scene_zigbee_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Leave Network",
        FuseRadioZigbeeMenuIndexLeaveNetwork,
        fuse_radio_scene_zigbee_menu_callback,
        app);

    submenu_set_selected_item(app->submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_zigbee_menu_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, FuseRadioSceneZigbeeMenu, event.event);

        if(event.event == FuseRadioZigbeeMenuIndexStatus) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeStatus);
        } else if(event.event == FuseRadioZigbeeMenuIndexScan) {
            fuse_radio_app_start_zigbee_scan(app);
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeStatus);
        } else if(event.event == FuseRadioZigbeeMenuIndexProfiles) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeProfiles);
        } else if(event.event == FuseRadioZigbeeMenuIndexCreateProfile) {
            snprintf(
                app->zigbee_name_input,
                sizeof(app->zigbee_name_input),
                "Profile %u",
                (unsigned)(app->zigbee_profile_count + 1U));
            app->zigbee_name_edit_mode = FuseRadioZigbeeNameEditCreateProfile;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeProfileName);
        } else if(event.event == FuseRadioZigbeeMenuIndexLeaveNetwork) {
            fuse_radio_app_start_zigbee_leave(app);
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeStatus);
        }

        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_zigbee_menu_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}
