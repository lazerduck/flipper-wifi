#include "../fuse_radio_app_i.h"

void fuse_radio_scene_wifi_promiscuous_watch_channel_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_channel_picker_view_set_channel(
        app->channel_picker_view, app->promiscuous_watch_channel);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewChannelPicker);
}

bool fuse_radio_scene_wifi_promiscuous_watch_channel_on_event(
    void* context,
    SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiWatchChannelStart) {
        fuse_radio_app_start_wifi_promiscuous_watch(app, app->promiscuous_watch_channel);
        scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiPromiscuousResult);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_promiscuous_watch_channel_on_exit(void* context) {
    FuseRadioApp* app = context;
    app->promiscuous_watch_channel =
        fuse_radio_channel_picker_view_get_channel(app->channel_picker_view);
}