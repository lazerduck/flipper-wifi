#include "../fuse_radio_app_i.h"

enum FuseRadioWifiPromiscuousMenuIndex {
    FuseRadioWifiPromiscuousMenuIndexSurveyQuick,
    FuseRadioWifiPromiscuousMenuIndexSurveyFull,
    FuseRadioWifiPromiscuousMenuIndexWatchCh1,
    FuseRadioWifiPromiscuousMenuIndexWatchCh6,
    FuseRadioWifiPromiscuousMenuIndexWatchCh11,
};

static void fuse_radio_scene_wifi_promiscuous_menu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_wifi_promiscuous_menu_on_enter(void* context) {
    FuseRadioApp* app = context;
    uint32_t selected =
        scene_manager_get_scene_state(app->scene_manager, FuseRadioSceneWifiPromiscuousMenu);

    if(app->wifi_mode != FuseRadioWifiModePromiscuous) {
        fuse_radio_app_start_wifi_promiscuous_enter(app, 1U);
    }

    submenu_add_item(
        app->submenu,
        "Survey 1/6/11",
        FuseRadioWifiPromiscuousMenuIndexSurveyQuick,
        fuse_radio_scene_wifi_promiscuous_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Survey 1-11",
        FuseRadioWifiPromiscuousMenuIndexSurveyFull,
        fuse_radio_scene_wifi_promiscuous_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Watch Ch 1",
        FuseRadioWifiPromiscuousMenuIndexWatchCh1,
        fuse_radio_scene_wifi_promiscuous_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Watch Ch 6",
        FuseRadioWifiPromiscuousMenuIndexWatchCh6,
        fuse_radio_scene_wifi_promiscuous_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Watch Ch 11",
        FuseRadioWifiPromiscuousMenuIndexWatchCh11,
        fuse_radio_scene_wifi_promiscuous_menu_callback,
        app);

    submenu_set_selected_item(app->submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_wifi_promiscuous_menu_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(
            app->scene_manager, FuseRadioSceneWifiPromiscuousMenu, event.event);

        if(event.event == FuseRadioWifiPromiscuousMenuIndexSurveyQuick) {
            fuse_radio_app_start_wifi_promiscuous_survey(app, true);
        } else if(event.event == FuseRadioWifiPromiscuousMenuIndexSurveyFull) {
            fuse_radio_app_start_wifi_promiscuous_survey(app, false);
        } else if(event.event == FuseRadioWifiPromiscuousMenuIndexWatchCh1) {
            fuse_radio_app_start_wifi_promiscuous_watch(app, 1U);
        } else if(event.event == FuseRadioWifiPromiscuousMenuIndexWatchCh6) {
            fuse_radio_app_start_wifi_promiscuous_watch(app, 6U);
        } else if(event.event == FuseRadioWifiPromiscuousMenuIndexWatchCh11) {
            fuse_radio_app_start_wifi_promiscuous_watch(app, 11U);
        }

        scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiPromiscuousResult);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        fuse_radio_app_start_wifi_promiscuous_exit(app);
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_promiscuous_menu_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}