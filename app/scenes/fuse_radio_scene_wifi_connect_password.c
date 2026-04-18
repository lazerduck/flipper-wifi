#include "../fuse_radio_app_i.h"

void fuse_radio_scene_wifi_connect_password_on_enter(void* context) {
    FuseRadioApp* app = context;

    app->text_input_mode = FuseRadioTextInputConnectPassword;
    text_input_set_header_text(
        app->text_input, app->connect_password_saved ? "Password (saved)" : "Password");
    text_input_set_result_callback(
        app->text_input,
        fuse_radio_app_text_input_callback,
        app,
        app->connect_password,
        sizeof(app->connect_password),
        true);
    text_input_set_minimum_length(app->text_input, 0U);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewTextInput);
}

bool fuse_radio_scene_wifi_connect_password_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventConnectPasswordDone) {
        app->connect_password_saved = app->connect_password[0] != '\0';
        app->connect_password_auto_used = false;
        if(strcmp(app->skip_auto_password_ssid, app->connect_ssid) == 0) {
            app->skip_auto_password_ssid[0] = '\0';
        }
        fuse_radio_app_start_wifi_connect(app);
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiConnectedMenu);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_connect_password_on_exit(void* context) {
    FuseRadioApp* app = context;
    text_input_reset(app->text_input);
    app->text_input_mode = FuseRadioTextInputNone;
}
