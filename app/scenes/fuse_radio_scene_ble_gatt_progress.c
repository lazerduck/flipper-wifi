#include "../fuse_radio_app_i.h"

void fuse_radio_scene_ble_gatt_progress_on_enter(void* context) {
    FuseRadioApp* app = context;

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, "GATT Inspect");
    widget_add_string_multiline_element(
        app->widget,
        64,
        32,
        AlignCenter,
        AlignCenter,
        FontSecondary,
        "Connecting to device...\nThis may take a moment.");

    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);

    fuse_radio_app_start_ble_gatt(app);
}

bool fuse_radio_scene_ble_gatt_progress_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FuseRadioCustomEventBleGattDone) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleGattServices);
            return true;
        }
        if(event.event == FuseRadioCustomEventBleGattFailed) {
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
                app->gatt_results.error[0] ? app->gatt_results.error : "Inspect failed");
            app->gatt_dirty = false;
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeTick && app->gatt_dirty) {
        /* Update connecting status message while waiting */
        if(app->gatt_results.active && !app->gatt_results.complete) {
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
                app->gatt_results.svc_count > 0U ? "Discovering characteristics..." :
                                                    "Connected. Discovering services...");
        }
        app->gatt_dirty = false;
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_ble_gatt_progress_on_exit(void* context) {
    FuseRadioApp* app = context;

    widget_reset(app->widget);
}
