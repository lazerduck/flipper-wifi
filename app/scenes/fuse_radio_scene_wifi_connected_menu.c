#include "../fuse_radio_app_i.h"

enum FuseRadioWifiConnectedMenuIndex {
    FuseRadioWifiConnectedMenuIndexDisconnect,
    FuseRadioWifiConnectedMenuIndexDiscover,
    FuseRadioWifiConnectedMenuIndexMdns,
};

static void fuse_radio_scene_wifi_connected_menu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_wifi_connected_menu_on_enter(void* context) {
    FuseRadioApp* app = context;
    uint32_t selected =
        scene_manager_get_scene_state(app->scene_manager, FuseRadioSceneWifiConnectedMenu);

    submenu_add_item(
        app->submenu,
        "Disconnect / Idle",
        FuseRadioWifiConnectedMenuIndexDisconnect,
        fuse_radio_scene_wifi_connected_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Discover Hosts",
        FuseRadioWifiConnectedMenuIndexDiscover,
        fuse_radio_scene_wifi_connected_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Query mDNS",
        FuseRadioWifiConnectedMenuIndexMdns,
        fuse_radio_scene_wifi_connected_menu_callback,
        app);

    submenu_set_selected_item(app->submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_wifi_connected_menu_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(
            app->scene_manager, FuseRadioSceneWifiConnectedMenu, event.event);

        if(event.event == FuseRadioWifiConnectedMenuIndexDisconnect) {
            fuse_radio_app_start_wifi_disconnect(app);
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiStatus);
        } else if(event.event == FuseRadioWifiConnectedMenuIndexDiscover) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiDiscoverResult);
        } else if(event.event == FuseRadioWifiConnectedMenuIndexMdns) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiMdnsHost);
        }

        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_connected_menu_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}