#include "../fuse_radio_app_i.h"

#include <string.h>

static void fuse_radio_scene_zigbee_button_picker_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_zigbee_button_picker_on_enter(void* context) {
    FuseRadioApp* app = context;

    if(app->zigbee_selected_profile_index >= app->zigbee_profile_count) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    const FuseRadioZigbeeProfile* profile = &app->zigbee_profiles[app->zigbee_selected_profile_index];

    submenu_set_header(app->submenu, "Rename Button");
    for(uint8_t i = 0U; i < FUSE_RADIO_ZIGBEE_BUTTON_COUNT; i++) {
        submenu_add_item(
            app->submenu,
            profile->buttons[i],
            i,
            fuse_radio_scene_zigbee_button_picker_callback,
            app);
    }

    submenu_set_selected_item(app->submenu, app->zigbee_selected_button_index);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_zigbee_button_picker_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(app->zigbee_selected_profile_index >= app->zigbee_profile_count) {
        return false;
    }

    if(event.type == SceneManagerEventTypeCustom &&
       event.event < FUSE_RADIO_ZIGBEE_BUTTON_COUNT) {
        const FuseRadioZigbeeProfile* profile = &app->zigbee_profiles[app->zigbee_selected_profile_index];
        app->zigbee_selected_button_index = (uint8_t)event.event;
        strncpy(
            app->zigbee_name_input,
            profile->buttons[app->zigbee_selected_button_index],
            sizeof(app->zigbee_name_input) - 1U);
        app->zigbee_name_input[sizeof(app->zigbee_name_input) - 1U] = '\0';
        app->zigbee_name_edit_mode = FuseRadioZigbeeNameEditRenameButton;
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeProfileName);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_zigbee_button_picker_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, NULL);
}
