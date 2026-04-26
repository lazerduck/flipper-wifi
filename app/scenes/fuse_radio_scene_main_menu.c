#include "../fuse_radio_app_i.h"

enum FuseRadioMainMenuIndex {
    FuseRadioMainMenuIndexWifi,
    FuseRadioMainMenuIndexBle,
    FuseRadioMainMenuIndexLed,
    FuseRadioMainMenuIndexZigbee,
};

static void fuse_radio_scene_main_menu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_main_menu_on_enter(void* context) {
    FuseRadioApp* app = context;
    uint32_t selected = scene_manager_get_scene_state(app->scene_manager, FuseRadioSceneMainMenu);

    submenu_add_item(
        app->submenu, "WiFi", FuseRadioMainMenuIndexWifi, fuse_radio_scene_main_menu_callback, app);
    submenu_add_item(
        app->submenu, "BLE", FuseRadioMainMenuIndexBle, fuse_radio_scene_main_menu_callback, app);
    submenu_add_item(
        app->submenu, "LED", FuseRadioMainMenuIndexLed, fuse_radio_scene_main_menu_callback, app);
    submenu_add_item(
        app->submenu,
        "Zigbee",
        FuseRadioMainMenuIndexZigbee,
        fuse_radio_scene_main_menu_callback,
        app);

    submenu_set_selected_item(app->submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_main_menu_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, FuseRadioSceneMainMenu, event.event);

        if(event.event == FuseRadioMainMenuIndexWifi) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiMenu);
        } else if(event.event == FuseRadioMainMenuIndexBle) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneBleMenu);
        } else if(event.event == FuseRadioMainMenuIndexLed) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneLedMenu);
        } else if(event.event == FuseRadioMainMenuIndexZigbee) {
            strncpy(app->placeholder_title, "Zigbee", sizeof(app->placeholder_title) - 1U);
            app->placeholder_title[sizeof(app->placeholder_title) - 1U] = '\0';
            strncpy(
                app->placeholder_message,
                "Menu slot is live.\nStack support comes later.",
                sizeof(app->placeholder_message) - 1U);
            app->placeholder_message[sizeof(app->placeholder_message) - 1U] = '\0';
            scene_manager_next_scene(app->scene_manager, FuseRadioScenePlaceholder);
        }

        return true;
    }

    return false;
}

void fuse_radio_scene_main_menu_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}
