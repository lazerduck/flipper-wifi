#include "../fuse_radio_app_i.h"

static bool fuse_radio_scene_wifi_promiscuous_survey_result_return_to_menu(FuseRadioApp* app) {
    return scene_manager_search_and_switch_to_previous_scene(
        app->scene_manager, FuseRadioSceneWifiPromiscuousMenu);
}

void fuse_radio_scene_wifi_promiscuous_survey_result_on_enter(void* context) {
    FuseRadioApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSurveyResult);
}

bool fuse_radio_scene_wifi_promiscuous_survey_result_on_event(
    void* context,
    SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiPromiscuousRepeat) {
        fuse_radio_app_repeat_wifi_promiscuous_action(app);
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, FuseRadioSceneWifiPromiscuousSurveyProgress);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return fuse_radio_scene_wifi_promiscuous_survey_result_return_to_menu(app);
    }

    return false;
}

void fuse_radio_scene_wifi_promiscuous_survey_result_on_exit(void* context) {
    FuseRadioApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}