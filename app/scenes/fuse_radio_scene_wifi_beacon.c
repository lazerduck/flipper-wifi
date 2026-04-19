#include "../fuse_radio_app_i.h"

#define BEACON_DEFAULT_CHANNEL    6U
#define BEACON_DEFAULT_DURATION_MS 30000U

static void fuse_radio_app_wifi_beacon_stop_callback(
    GuiButtonType result,
    InputType type,
    void* context);

static void fuse_radio_scene_wifi_beacon_update_widget(FuseRadioApp* app) {
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 0, AlignCenter, AlignTop, FontPrimary, "Fake Beacons");
    widget_add_string_multiline_element(
        app->widget, 64, 20, AlignCenter, AlignTop, FontSecondary, app->beacon_info_text);

    if(app->beacon_active && !app->beacon_stop_pending) {
        widget_add_button_element(
            app->widget,
            GuiButtonTypeCenter,
            "Stop",
            fuse_radio_app_wifi_beacon_stop_callback,
            app);
    }
}

static void fuse_radio_app_wifi_beacon_stop_callback(GuiButtonType result, InputType type, void* context) {
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        FuseRadioApp* app = context;
        fuse_radio_app_stop_wifi_beacon(app);
        fuse_radio_scene_wifi_beacon_update_widget(app);
    }
}

void fuse_radio_scene_wifi_beacon_on_enter(void* context) {
    FuseRadioApp* app = context;

    app->beacon_active = false;
    app->beacon_stop_pending = false;
    snprintf(
        app->beacon_info_text,
        sizeof(app->beacon_info_text),
        "Ch %u  %us\nStarting...",
        (unsigned)BEACON_DEFAULT_CHANNEL,
        (unsigned)(BEACON_DEFAULT_DURATION_MS / 1000U));

    fuse_radio_scene_wifi_beacon_update_widget(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);

    fuse_radio_app_start_wifi_beacon(app, BEACON_DEFAULT_CHANNEL, BEACON_DEFAULT_DURATION_MS);
}

bool fuse_radio_scene_wifi_beacon_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FuseRadioCustomEventWifiBeaconStarted ||
           event.event == FuseRadioCustomEventWifiBeaconDone ||
           event.event == FuseRadioCustomEventWifiBeaconFailed) {
            fuse_radio_scene_wifi_beacon_update_widget(app);
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        if(app->beacon_active && !app->beacon_stop_pending) {
            fuse_radio_app_stop_wifi_beacon(app);
            fuse_radio_scene_wifi_beacon_update_widget(app);
            return true; /* stay until BEACON_DONE arrives */
        }

        if(app->beacon_stop_pending) {
            return true; /* swallow back — waiting for stop confirmation */
        }

        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_beacon_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
}
