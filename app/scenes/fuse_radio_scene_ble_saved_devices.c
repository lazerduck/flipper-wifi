#include "../fuse_radio_app_i.h"

void fuse_radio_scene_ble_saved_devices_on_enter(void* context) {
    FuseRadioApp* app = context;

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, "Saved BLE");
    widget_add_string_multiline_element(
        app->widget,
        64,
        26,
        AlignCenter,
        AlignCenter,
        FontSecondary,
        "Owned devices will live here.\nLater this becomes strength\nchecks, presence refresh,\nand GATT entry points.");
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_ble_saved_devices_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_saved_devices_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
}