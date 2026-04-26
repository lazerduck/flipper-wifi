#include "../fuse_radio_app_i.h"

#include <stdio.h>

enum FuseRadioLedMenuIndex {
    FuseRadioLedMenuIndexRed,
    FuseRadioLedMenuIndexGreen,
    FuseRadioLedMenuIndexBlue,
    FuseRadioLedMenuIndexAuto,
};

static void fuse_radio_scene_led_menu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void fuse_radio_scene_led_menu_populate(FuseRadioApp* app) {
    char red_label[16];
    char green_label[16];
    char blue_label[16];
    uint32_t selected = scene_manager_get_scene_state(app->scene_manager, FuseRadioSceneLedMenu);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, app->led_manual_override ? "LED Manual" : "LED Auto");

    snprintf(red_label, sizeof(red_label), "Red: %u", (unsigned)app->led_red);
    snprintf(green_label, sizeof(green_label), "Green: %u", (unsigned)app->led_green);
    snprintf(blue_label, sizeof(blue_label), "Blue: %u", (unsigned)app->led_blue);

    submenu_add_item(
        app->submenu,
        red_label,
        FuseRadioLedMenuIndexRed,
        fuse_radio_scene_led_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        green_label,
        FuseRadioLedMenuIndexGreen,
        fuse_radio_scene_led_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        blue_label,
        FuseRadioLedMenuIndexBlue,
        fuse_radio_scene_led_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Return To Auto",
        FuseRadioLedMenuIndexAuto,
        fuse_radio_scene_led_menu_callback,
        app);
    submenu_set_selected_item(app->submenu, selected);
}

void fuse_radio_scene_led_menu_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_app_request_led_status(app);
    fuse_radio_scene_led_menu_populate(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_led_menu_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, FuseRadioSceneLedMenu, event.event);

        if(event.event == FuseRadioLedMenuIndexRed) {
            app->led_edit_channel = FuseRadioLedChannelRed;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneLedValue);
            return true;
        } else if(event.event == FuseRadioLedMenuIndexGreen) {
            app->led_edit_channel = FuseRadioLedChannelGreen;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneLedValue);
            return true;
        } else if(event.event == FuseRadioLedMenuIndexBlue) {
            app->led_edit_channel = FuseRadioLedChannelBlue;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneLedValue);
            return true;
        } else if(event.event == FuseRadioLedMenuIndexAuto) {
            app->led_manual_override = false;
            fuse_radio_app_set_led_auto(app);
            fuse_radio_scene_led_menu_populate(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_led_menu_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}