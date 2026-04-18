#include "../fuse_radio_app_i.h"

enum FuseRadioWifiConnectedMenuIndex {
    FuseRadioWifiConnectedMenuIndexDisconnect,
    FuseRadioWifiConnectedMenuIndexDiscover,
    FuseRadioWifiConnectedMenuIndexHttpIp,
    FuseRadioWifiConnectedMenuIndexHttpTime,
    FuseRadioWifiConnectedMenuIndexHttpLocation,
    FuseRadioWifiConnectedMenuIndexMdns,
};

static void fuse_radio_scene_wifi_connected_menu_callback(void* context, uint32_t index) {
    FuseRadioApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static bool fuse_radio_scene_wifi_connected_menu_leave_branch(FuseRadioApp* app) {
    fuse_radio_app_start_wifi_disconnect(app);

    if(scene_manager_search_and_switch_to_previous_scene(
           app->scene_manager, FuseRadioSceneWifiMenu)) {
        return true;
    }

    scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiMenu);
    return true;
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
        "Public IP",
        FuseRadioWifiConnectedMenuIndexHttpIp,
        fuse_radio_scene_wifi_connected_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Get Time",
        FuseRadioWifiConnectedMenuIndexHttpTime,
        fuse_radio_scene_wifi_connected_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Get Location",
        FuseRadioWifiConnectedMenuIndexHttpLocation,
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
            return fuse_radio_scene_wifi_connected_menu_leave_branch(app);
        } else if(event.event == FuseRadioWifiConnectedMenuIndexDiscover) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiDiscoverProgress);
        } else if(event.event == FuseRadioWifiConnectedMenuIndexHttpIp) {
            app->http_preset = FuseRadioHttpPresetPublicIp;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiHttpResult);
        } else if(event.event == FuseRadioWifiConnectedMenuIndexHttpTime) {
            app->http_preset = FuseRadioHttpPresetTime;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiHttpResult);
        } else if(event.event == FuseRadioWifiConnectedMenuIndexHttpLocation) {
            app->http_preset = FuseRadioHttpPresetLocation;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiHttpResult);
        } else if(event.event == FuseRadioWifiConnectedMenuIndexMdns) {
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiMdnsHost);
        }

        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return fuse_radio_scene_wifi_connected_menu_leave_branch(app);
    }

    return false;
}

void fuse_radio_scene_wifi_connected_menu_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
}