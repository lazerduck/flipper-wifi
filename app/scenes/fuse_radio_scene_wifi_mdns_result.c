#include "../fuse_radio_app_i.h"

static void fuse_radio_scene_wifi_mdns_button_callback(
    GuiButtonType button,
    InputType type,
    void* context) {
    FuseRadioApp* app = context;

    if(button == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiMdnsRefresh);
    }
}

void fuse_radio_scene_wifi_mdns_result_on_enter(void* context) {
    FuseRadioApp* app = context;

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, "mDNS");
    widget_add_text_scroll_element(app->widget, 0, 15, 128, 38, app->mdns_info_text);
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Query", fuse_radio_scene_wifi_mdns_button_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_wifi_mdns_result_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiMdnsRefresh) {
        fuse_radio_app_start_wifi_mdns_query(app);
        fuse_radio_app_refresh_mdns_widget(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_mdns_result_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
}
