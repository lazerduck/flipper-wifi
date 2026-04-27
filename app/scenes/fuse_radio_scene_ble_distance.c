#include "../fuse_radio_app_i.h"

static void fuse_radio_scene_ble_distance_show(FuseRadioApp* app) {
    fuse_radio_app_refresh_ble_distance_widget(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

void fuse_radio_scene_ble_distance_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_app_start_ble_distance(app);
    fuse_radio_scene_ble_distance_show(app);
}

bool fuse_radio_scene_ble_distance_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventBleDistanceStop) {
        if(app->ble_distance_active) {
            fuse_radio_app_stop_ble_distance(app);
        } else {
            fuse_radio_app_start_ble_distance(app);
        }
        fuse_radio_scene_ble_distance_show(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventBleDistanceBack) {
        if(app->ble_distance_active) {
            fuse_radio_app_stop_ble_distance(app);
            fuse_radio_scene_ble_distance_show(app);
            return true;
        }

        return scene_manager_previous_scene(app->scene_manager);
    }

    if(event.type == SceneManagerEventTypeBack) {
        if(app->ble_distance_active) {
            fuse_radio_app_stop_ble_distance(app);
            fuse_radio_scene_ble_distance_show(app);
            return true;
        }

        if(app->ble_distance_stop_pending) {
            return true;
        }

        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_distance_on_exit(void* context) {
    FuseRadioApp* app = context;

    widget_reset(app->widget);
}
