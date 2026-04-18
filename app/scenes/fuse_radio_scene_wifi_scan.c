#include "../fuse_radio_app_i.h"

void fuse_radio_scene_wifi_scan_on_enter(void* context) {
    FuseRadioApp* app = context;
    fuse_radio_scan_view_set_mode(app->scan_view, FuseRadioScanViewModeBrowse);
    fuse_radio_app_start_wifi_scan(app);
    fuse_radio_app_refresh_scan_view(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewScan);
}

bool fuse_radio_scene_wifi_scan_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == FuseRadioCustomEventScanRescan) {
        fuse_radio_app_start_wifi_scan(app);
        fuse_radio_app_refresh_scan_view(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_scan_on_exit(void* context) {
    FuseRadioApp* app = context;
    UNUSED(app);
}
