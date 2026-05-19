#include "../app.h"

typedef enum {
    MainMenuIndexWifi,
    MainMenuIndexBle,
    MainMenuIndexZigbee,
    MainMenuIndexLed,
    MainMenuIndexSdCard,
    MainMenuIndexSettings,
} MainMenuIndex;

static void main_menu_cb(void* ctx, uint32_t index) {
    App* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void app_scene_main_menu_on_enter(void* ctx) {
    App* app = ctx;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Fuse Radio");
    submenu_add_item(app->submenu, "WiFi",     MainMenuIndexWifi,     main_menu_cb, app);
    submenu_add_item(app->submenu, "BLE",      MainMenuIndexBle,      main_menu_cb, app);
    submenu_add_item(app->submenu, "ZigBee",   MainMenuIndexZigbee,   main_menu_cb, app);
    submenu_add_item(app->submenu, "LED",      MainMenuIndexLed,      main_menu_cb, app);
    submenu_add_item(app->submenu, "SD Card",  MainMenuIndexSdCard,   main_menu_cb, app);
    submenu_add_item(app->submenu, "Settings", MainMenuIndexSettings, main_menu_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewSubmenu);
}

bool app_scene_main_menu_on_event(void* ctx, SceneManagerEvent event) {
    App* app      = ctx;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        /* Exit the app cleanly rather than popping back to the boot scene */
        view_dispatcher_stop(app->view_dispatcher);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        switch((MainMenuIndex)event.event) {
        case MainMenuIndexWifi:
        case MainMenuIndexBle:
        case MainMenuIndexZigbee:
            /* TODO: navigate to respective scene */
            consumed = true;
            break;
        case MainMenuIndexLed:
            scene_manager_next_scene(app->scene_manager, AppSceneLed);
            consumed = true;
            break;
        case MainMenuIndexSdCard:
        case MainMenuIndexSettings:
            /* Sub-scenes will be wired in as features are implemented */
            consumed = true;
            break;
        default:
            break;
        }
    }

    return consumed;
}

void app_scene_main_menu_on_exit(void* ctx) {
    App* app = ctx;
    submenu_reset(app->submenu);
}
