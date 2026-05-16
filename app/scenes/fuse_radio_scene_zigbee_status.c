#include "../fuse_radio_app_i.h"

static void fuse_radio_scene_zigbee_status_button_callback(
    GuiButtonType button,
    InputType type,
    void* context) {
    FuseRadioApp* app = context;

    if(type != InputTypeShort) {
        return;
    }

    if(button == GuiButtonTypeCenter) {
        fuse_radio_app_start_zigbee_status(app);
    } else if(button == GuiButtonTypeLeft) {
        scene_manager_previous_scene(app->scene_manager);
    }
}

static void fuse_radio_scene_zigbee_status_refresh_widget(FuseRadioApp* app) {
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, "Zigbee");
    widget_add_text_scroll_element(app->widget, 0, 14, 128, 38, app->zigbee_info_text);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeLeft,
        "Back",
        fuse_radio_scene_zigbee_status_button_callback,
        app);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "Refresh",
        fuse_radio_scene_zigbee_status_button_callback,
        app);
    app->zigbee_dirty = false;
}

void fuse_radio_scene_zigbee_status_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_app_start_zigbee_status(app);
    fuse_radio_scene_zigbee_status_refresh_widget(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_zigbee_status_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == FuseRadioCustomEventZigbeeRefresh) {
        if(app->zigbee_dirty) {
            fuse_radio_scene_zigbee_status_refresh_widget(app);
        }
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom && event.event == FuseRadioCustomEventZigbeeJoinFailed) {
        /* show the error in the widget then let the user go back */
        if(app->zigbee_dirty) {
            fuse_radio_scene_zigbee_status_refresh_widget(app);
        }
        /* Add an extra hint so the user knows to press Back */
        widget_add_string_element(
            app->widget, 64, 52, AlignCenter, AlignBottom, FontSecondary, "Press Back to retry");
        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_zigbee_status_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
}
