#include "../fuse_radio_app_i.h"

enum FuseRadioWifiMenuIndex {
    FuseRadioWifiMenuIndexScan,
    FuseRadioWifiMenuIndexStatus,
    FuseRadioWifiMenuIndexConnect,
    FuseRadioWifiMenuIndexDisconnect,
    FuseRadioWifiMenuIndexDiscover,
    FuseRadioWifiMenuIndexMdns,
};

static void fuse_radio_scene_wifi_menu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void fuse_radio_scene_wifi_menu_on_enter(void* context) {
    FuseRadioApp* app = context;
    uint32_t selected = scene_manager_get_scene_state(app->scene_manager, FuseRadioSceneWifiMenu);

    submenu_add_item(
        app->submenu,
        "Scan Nearby APs",
        FuseRadioWifiMenuIndexScan,
        fuse_radio_scene_wifi_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Connection Status",
        FuseRadioWifiMenuIndexStatus,
        fuse_radio_scene_wifi_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Connect to AP",
        FuseRadioWifiMenuIndexConnect,
        fuse_radio_scene_wifi_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Disconnect",
        FuseRadioWifiMenuIndexDisconnect,
        fuse_radio_scene_wifi_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Discover Hosts",
        FuseRadioWifiMenuIndexDiscover,
        fuse_radio_scene_wifi_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Query mDNS",
        FuseRadioWifiMenuIndexMdns,
        fuse_radio_scene_wifi_menu_callback,
        app);

    submenu_set_selected_item(app->submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewSubmenu);
}

bool fuse_radio_scene_wifi_menu_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, FuseRadioSceneWifiMenu, event.event);

        if(event.event == FuseRadioWifiMenuIndexScan) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiScan);
        } else if(event.event == FuseRadioWifiMenuIndexStatus) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiStatus);
        } else if(event.event == FuseRadioWifiMenuIndexConnect) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiConnectSsid);
        } else if(event.event == FuseRadioWifiMenuIndexDisconnect) {
            fuse_radio_app_start_wifi_disconnect(app);
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiStatus);
        } else if(event.event == FuseRadioWifiMenuIndexDiscover) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiDiscoverResult);
        } else if(event.event == FuseRadioWifiMenuIndexMdns) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiMdnsHost);
        }

        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_menu_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}