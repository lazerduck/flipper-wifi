#include "../fuse_radio_app_i.h"

void fuse_radio_scene_wifi_promiscuous_survey_progress_on_enter(void* context) {
    FuseRadioApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSurveyProgress);
}

bool fuse_radio_scene_wifi_promiscuous_survey_progress_on_event(
    void* context,
    SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiSurveyDone) {
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiPromiscuousSurveyResult);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiSurveyFailed) {
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiPromiscuousResult);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return true;
    }

    return false;
}

void fuse_radio_scene_wifi_promiscuous_survey_progress_on_exit(void* context) {
    FuseRadioApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}