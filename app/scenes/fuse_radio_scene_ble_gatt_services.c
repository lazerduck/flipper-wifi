#include "../fuse_radio_app_i.h"

void fuse_radio_scene_ble_gatt_services_on_enter(void* context) {
    FuseRadioApp* app = context;

    if(app->gatt_results.svc_count == 0U) {
        widget_reset(app->widget);
        widget_add_string_element(
            app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, "GATT Inspect");
        widget_add_string_multiline_element(
            app->widget,
            64,
            32,
            AlignCenter,
            AlignCenter,
            FontSecondary,
            "No services found");
        view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
        return;
    }

    fuse_radio_app_refresh_gatt_services_submenu(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_ble_gatt_services_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event < app->gatt_results.svc_count) {
            app->ble_gatt_selected_svc = (uint8_t)event.event;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleGattCharacteristics);
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_gatt_services_on_exit(void* context) {
    FuseRadioApp* app = context;

    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
