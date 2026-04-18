#include "../fuse_radio_app_i.h"

void fuse_radio_scene_wifi_mdns_host_on_enter(void* context) {
    FuseRadioApp* app = context;

    app->text_input_mode = FuseRadioTextInputMdnsHost;
    text_input_set_header_text(app->text_input, "mDNS Host");
    text_input_set_result_callback(
        app->text_input,
        fuse_radio_app_text_input_callback,
        app,
        app->mdns_host,
        sizeof(app->mdns_host),
        false);
    text_input_set_minimum_length(app->text_input, 1U);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewTextInput);
}

bool fuse_radio_scene_wifi_mdns_host_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventMdnsHostDone) {
        fuse_radio_app_start_wifi_mdns_query(app);
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiMdnsResult);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_mdns_host_on_exit(void* context) {
    FuseRadioApp* app = context;
    text_input_reset(app->text_input);
    app->text_input_mode = FuseRadioTextInputNone;
}
