#include "../fuse_radio_app_i.h"

#include <string.h>

enum FuseRadioZigbeeProfileActionIndex {
    FuseRadioZigbeeProfileActionIndexJoin,
    FuseRadioZigbeeProfileActionIndexRenameProfile,
    FuseRadioZigbeeProfileActionIndexRenameButton,
    FuseRadioZigbeeProfileActionIndexDeleteProfile,
    FuseRadioZigbeeProfileActionIndexTrigger1,
    FuseRadioZigbeeProfileActionIndexTrigger2,
    FuseRadioZigbeeProfileActionIndexTrigger3,
    FuseRadioZigbeeProfileActionIndexTrigger4,
    FuseRadioZigbeeProfileActionIndexTrigger5,
    FuseRadioZigbeeProfileActionIndexTrigger6,
    FuseRadioZigbeeProfileActionIndexTrigger7,
    FuseRadioZigbeeProfileActionIndexTrigger8,
};

static void fuse_radio_scene_zigbee_profile_actions_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_zigbee_profile_actions_on_enter(void* context) {
    FuseRadioApp* app = context;
    if(app->zigbee_selected_profile_index >= app->zigbee_profile_count) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    const FuseRadioZigbeeProfile* profile = &app->zigbee_profiles[app->zigbee_selected_profile_index];

    submenu_set_header(app->submenu, profile->name);
    submenu_add_item(
        app->submenu,
        "Join Network",
        FuseRadioZigbeeProfileActionIndexJoin,
        fuse_radio_scene_zigbee_profile_actions_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Rename Profile",
        FuseRadioZigbeeProfileActionIndexRenameProfile,
        fuse_radio_scene_zigbee_profile_actions_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Rename Button",
        FuseRadioZigbeeProfileActionIndexRenameButton,
        fuse_radio_scene_zigbee_profile_actions_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Delete Profile",
        FuseRadioZigbeeProfileActionIndexDeleteProfile,
        fuse_radio_scene_zigbee_profile_actions_callback,
        app);

    for(uint8_t i = 0U; i < FUSE_RADIO_ZIGBEE_BUTTON_COUNT; i++) {
        submenu_add_item(
            app->submenu,
            profile->buttons[i],
            FuseRadioZigbeeProfileActionIndexTrigger1 + i,
            fuse_radio_scene_zigbee_profile_actions_callback,
            app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_zigbee_profile_actions_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;
    if(app->zigbee_selected_profile_index >= app->zigbee_profile_count) {
        return false;
    }

    const FuseRadioZigbeeProfile* profile = &app->zigbee_profiles[app->zigbee_selected_profile_index];

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FuseRadioZigbeeProfileActionIndexJoin) {
            fuse_radio_app_start_zigbee_join_profile(app, profile->id);
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeStatus);
            return true;
        }

        if(event.event == FuseRadioZigbeeProfileActionIndexRenameProfile) {
            strncpy(
                app->zigbee_name_input,
                profile->name,
                sizeof(app->zigbee_name_input) - 1U);
            app->zigbee_name_input[sizeof(app->zigbee_name_input) - 1U] = '\0';
            app->zigbee_name_edit_mode = FuseRadioZigbeeNameEditRenameProfile;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeProfileName);
            return true;
        }

        if(event.event == FuseRadioZigbeeProfileActionIndexRenameButton) {
            app->zigbee_selected_button_index = 0U;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeButtonPicker);
            return true;
        }

        if(event.event == FuseRadioZigbeeProfileActionIndexDeleteProfile) {
            fuse_radio_app_start_zigbee_delete_profile(app, profile->id);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, FuseRadioSceneZigbeeProfiles);
            return true;
        }

        if(event.event >= FuseRadioZigbeeProfileActionIndexTrigger1 &&
           event.event <= FuseRadioZigbeeProfileActionIndexTrigger8) {
            uint8_t button_index = (uint8_t)(event.event - FuseRadioZigbeeProfileActionIndexTrigger1);
            fuse_radio_app_start_zigbee_trigger(app, profile->id, button_index);
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeStatus);
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_zigbee_profile_actions_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, NULL);
}
