#include "../fuse_radio_app_i.h"

void fuse_radio_scene_ble_scan_on_enter(void* context) {
    FuseRadioApp* app = context;

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, "BLE Scan");
    widget_add_string_multiline_element(
        app->widget,
        64,
        26,
        AlignCenter,
        AlignCenter,
        FontSecondary,
        "First BLE slice.\nMenu and protocol are wired.\nScan results will feed\nBLE zoo and device actions.");
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_ble_scan_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_scan_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
}