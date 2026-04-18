#include "../fuse_radio_app_i.h"

void fuse_radio_scene_wifi_promiscuous_survey_preset_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_survey_preset_view_set_preset(app->survey_preset_view, app->survey_results.preset);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSurveyPreset);
}

bool fuse_radio_scene_wifi_promiscuous_survey_preset_on_event(
    void* context,
    SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiSurveyPresetStart) {
        fuse_radio_app_start_wifi_promiscuous_survey(app, app->survey_results.preset);
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiPromiscuousSurveyProgress);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_promiscuous_survey_preset_on_exit(void* context) {
    FuseRadioApp* app = context;
    app->survey_results.preset = fuse_radio_survey_preset_view_get_preset(app->survey_preset_view);
}