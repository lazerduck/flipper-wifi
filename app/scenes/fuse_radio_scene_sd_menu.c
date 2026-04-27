#include "../fuse_radio_app_i.h"

#include <stdio.h>

enum FuseRadioSdMenuIndex {
    FuseRadioSdMenuIndexExplore,
    FuseRadioSdMenuIndexDetail,
};

static void fuse_radio_scene_sd_menu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_sd_menu_on_enter(void* context) {
    FuseRadioApp* app = context;
    uint32_t selected = scene_manager_get_scene_state(app->scene_manager, FuseRadioSceneSdMenu);

    submenu_add_item(
        app->submenu,
        "Explore",
        FuseRadioSdMenuIndexExplore,
        fuse_radio_scene_sd_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Detail",
        FuseRadioSdMenuIndexDetail,
        fuse_radio_scene_sd_menu_callback,
        app);

    submenu_set_selected_item(app->submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_sd_menu_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, FuseRadioSceneSdMenu, event.event);

        if(event.event == FuseRadioSdMenuIndexExplore) {
            if(app->sd_explore_path[0] == '\0') {
                snprintf(app->sd_explore_path, sizeof(app->sd_explore_path), "/");
            }
            app->sd_action = FuseRadioSdActionExplore;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneSdExplore);
        } else if(event.event == FuseRadioSdMenuIndexDetail) {
            app->sd_action = FuseRadioSdActionDetail;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneSdResult);
        }

        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_sd_menu_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}
