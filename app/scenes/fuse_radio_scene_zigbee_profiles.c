#include "../fuse_radio_app_i.h"

enum FuseRadioZigbeeProfilesIndex {
    FuseRadioZigbeeProfilesIndexRefresh = 1000U,
};

static void fuse_radio_scene_zigbee_profiles_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void fuse_radio_scene_zigbee_profiles_rebuild(FuseRadioApp* app) {
    submenu_reset(app->submenu);

    submenu_add_item(
        app->submenu,
        "Refresh",
        FuseRadioZigbeeProfilesIndexRefresh,
        fuse_radio_scene_zigbee_profiles_callback,
        app);

    for(uint8_t i = 0U; i < app->zigbee_profile_count; i++) {
        const FuseRadioZigbeeProfile* profile = &app->zigbee_profiles[i];
        submenu_add_item(app->submenu, profile->name, i, fuse_radio_scene_zigbee_profiles_callback, app);
    }
}

void fuse_radio_scene_zigbee_profiles_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_app_start_zigbee_list(app);
    fuse_radio_scene_zigbee_profiles_rebuild(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_zigbee_profiles_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == FuseRadioCustomEventZigbeeRefresh) {
        fuse_radio_scene_zigbee_profiles_rebuild(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FuseRadioZigbeeProfilesIndexRefresh) {
            fuse_radio_app_start_zigbee_list(app);
            return true;
        }

        if(event.event < app->zigbee_profile_count) {
            app->zigbee_selected_profile_index = (uint8_t)event.event;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneZigbeeProfileActions);
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_zigbee_profiles_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}
