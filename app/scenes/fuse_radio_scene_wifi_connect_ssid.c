#include "../fuse_radio_app_i.h"

void fuse_radio_scene_wifi_connect_ssid_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_scan_view_set_mode(app->scan_view, FuseRadioScanViewModeSelect);
    memset(app->connect_password, 0, sizeof(app->connect_password));
    fuse_radio_app_start_wifi_scan(app);
    fuse_radio_app_refresh_scan_view(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewScan);
}

bool fuse_radio_scene_wifi_connect_ssid_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FuseRadioCustomEventScanRescan) {
            fuse_radio_app_start_wifi_scan(app);
            fuse_radio_app_refresh_scan_view(app);
            return true;
        }

        if(event.event == FuseRadioCustomEventScanSelect) {
            if(app->scan_results.count == 0U) {
                return true;
            }

            const uint8_t selected_index = fuse_radio_scan_view_get_selected_index(app->scan_view);
            const FuseRadioAccessPoint* ap = &app->scan_results.aps[selected_index];
            const bool is_open_network = strcmp(ap->auth, "OPEN") == 0;

            if(!ap->ssid[0]) {
                strncpy(
                    app->placeholder_title, "Hidden SSID", sizeof(app->placeholder_title) - 1U);
                app->placeholder_title[sizeof(app->placeholder_title) - 1U] = '\0';
                strncpy(
                    app->placeholder_message,
                    "Hidden networks need\nmanual support later.",
                    sizeof(app->placeholder_message) - 1U);
                app->placeholder_message[sizeof(app->placeholder_message) - 1U] = '\0';
                scene_manager_next_scene(app->scene_manager, FuseRadioScenePlaceholder);
                return true;
            }

            strncpy(app->connect_ssid, ap->ssid, sizeof(app->connect_ssid) - 1U);
            app->connect_ssid[sizeof(app->connect_ssid) - 1U] = '\0';

            const char* saved_password = !is_open_network ? fuse_radio_app_get_saved_password(app, ap->ssid) : NULL;
            if(saved_password) {
                strncpy(app->connect_password, saved_password, sizeof(app->connect_password) - 1U);
                app->connect_password[sizeof(app->connect_password) - 1U] = '\0';
                app->connect_password_saved = true;
            } else {
                memset(app->connect_password, 0, sizeof(app->connect_password));
                app->connect_password_saved = false;
            }

            if(is_open_network) {
                app->connect_password_auto_used = false;
                if(strcmp(app->skip_auto_password_ssid, ap->ssid) == 0) {
                    app->skip_auto_password_ssid[0] = '\0';
                }
                fuse_radio_app_start_wifi_connect(app);
                scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiConnectedMenu);
                return true;
            }

            if(saved_password && strcmp(app->skip_auto_password_ssid, ap->ssid) != 0) {
                app->connect_password_auto_used = true;
                fuse_radio_app_start_wifi_connect(app);
                scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiConnectedMenu);
                return true;
            }

            app->connect_password_auto_used = false;
            scene_manager_next_scene(app->scene_manager, FuseRadioSceneWifiConnectPassword);
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_wifi_connect_ssid_on_exit(void* context) {
    FuseRadioApp* app = context;
    fuse_radio_scan_view_set_mode(app->scan_view, FuseRadioScanViewModeBrowse);
}
