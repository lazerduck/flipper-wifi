#include "../fuse_radio_app_i.h"

void fuse_radio_scene_zigbee_profile_name_on_enter(void* context) {
    FuseRadioApp* app = context;
    const char* header = "Profile Name";

    if(app->zigbee_name_edit_mode == FuseRadioZigbeeNameEditCreateProfile) {
        header = "Create Profile";
    } else if(app->zigbee_name_edit_mode == FuseRadioZigbeeNameEditRenameProfile) {
        header = "Rename Profile";
    } else if(app->zigbee_name_edit_mode == FuseRadioZigbeeNameEditRenameButton) {
        header = "Rename Button";
    }

    app->text_input_mode = FuseRadioTextInputZigbeeName;
    text_input_set_header_text(app->text_input, header);
    text_input_set_result_callback(
        app->text_input,
        fuse_radio_app_text_input_callback,
        app,
        app->zigbee_name_input,
        sizeof(app->zigbee_name_input),
        false);
    text_input_set_minimum_length(app->text_input, 1U);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewTextInput);
}

bool fuse_radio_scene_zigbee_profile_name_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == FuseRadioCustomEventZigbeeNameDone) {
        bool started = false;

        if(app->zigbee_name_edit_mode == FuseRadioZigbeeNameEditCreateProfile) {
            started = fuse_radio_app_start_zigbee_create_profile(app, app->zigbee_name_input);
        } else if(app->zigbee_name_edit_mode == FuseRadioZigbeeNameEditRenameProfile) {
            if(app->zigbee_selected_profile_index < app->zigbee_profile_count) {
                const FuseRadioZigbeeProfile* profile =
                    &app->zigbee_profiles[app->zigbee_selected_profile_index];
                started =
                    fuse_radio_app_start_zigbee_rename_profile(app, profile->id, app->zigbee_name_input);
            }
        } else if(app->zigbee_name_edit_mode == FuseRadioZigbeeNameEditRenameButton) {
            if(app->zigbee_selected_profile_index < app->zigbee_profile_count) {
                const FuseRadioZigbeeProfile* profile =
                    &app->zigbee_profiles[app->zigbee_selected_profile_index];
                started = fuse_radio_app_start_zigbee_rename_button(
                    app,
                    profile->id,
                    app->zigbee_selected_button_index,
                    app->zigbee_name_input);
            }
        }

        if(started) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeStatus);
        } else {
            scene_manager_previous_scene(app->scene_manager);
        }

        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_zigbee_profile_name_on_exit(void* context) {
    FuseRadioApp* app = context;
    text_input_reset(app->text_input);
    app->text_input_mode = FuseRadioTextInputNone;
    app->zigbee_name_edit_mode = FuseRadioZigbeeNameEditNone;
}
