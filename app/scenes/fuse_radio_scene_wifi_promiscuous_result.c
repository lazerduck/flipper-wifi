#include "../fuse_radio_app_i.h"

static bool fuse_radio_scene_wifi_promiscuous_return_to_menu(FuseRadioApp* app) {
    return scene_manager_search_and_switch_to_previous_scene(
        app->scene_manager, FuseRadioSceneWifiPromiscuousMenu);
}

static void fuse_radio_scene_wifi_promiscuous_show_current_view(FuseRadioApp* app) {
    fuse_radio_app_refresh_promiscuous_widget(app);
    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        app->promiscuous_watch_live_active ?
            FuseRadioViewWatchLive :
            (app->watch_summary.has_summary && app->watch_device_count > 0U ?
                 FuseRadioViewWatchResult :
                 FuseRadioViewWidget));
}

void fuse_radio_scene_wifi_promiscuous_result_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_scene_wifi_promiscuous_show_current_view(app);
}

bool fuse_radio_scene_wifi_promiscuous_result_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiPromiscuousRepeat) {
        fuse_radio_app_repeat_wifi_promiscuous_action(app);
        fuse_radio_scene_wifi_promiscuous_show_current_view(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiPromiscuousStop) {
        fuse_radio_app_stop_wifi_promiscuous_watch(app);
        fuse_radio_scene_wifi_promiscuous_show_current_view(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventWifiPromiscuousMenu) {
        return fuse_radio_scene_wifi_promiscuous_return_to_menu(app);
    }

    if(event.type == SceneManagerEventTypeBack) {
        if(app->promiscuous_watch_live_active) {
            fuse_radio_app_stop_wifi_promiscuous_watch(app);
            fuse_radio_scene_wifi_promiscuous_show_current_view(app);
            return true;
        }

        if(app->promiscuous_watch_stop_pending) {
            return true;
        }

        return fuse_radio_scene_wifi_promiscuous_return_to_menu(app);
    }

    return false;
}

void fuse_radio_scene_wifi_promiscuous_result_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}