#include "../../app.h"

typedef enum {
    SdMenuDetails,
    SdMenuExplorer,
    SdMenuFormat,
} SdMenu;

static void sd_menu_cb(void* ctx, uint32_t index) {
    App* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void app_scene_sd_card_on_enter(void* ctx) {
    App* app = ctx;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "SD Card");
    submenu_add_item(app->submenu, "Details",  SdMenuDetails,  sd_menu_cb, app);
    submenu_add_item(app->submenu, "Explorer", SdMenuExplorer, sd_menu_cb, app);
    submenu_add_item(app->submenu, "Format",   SdMenuFormat,   sd_menu_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewSubmenu);
}

bool app_scene_sd_card_on_event(void* ctx, SceneManagerEvent event) {
    App* app      = ctx;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch((SdMenu)event.event) {
        case SdMenuDetails:
            scene_manager_next_scene(app->scene_manager, AppSceneSdDetails);
            consumed = true;
            break;
        case SdMenuExplorer:
            /* Reset to root each time we open the explorer from the menu */
            app->sd_path[0] = '/';
            app->sd_path[1] = '\0';
            scene_manager_next_scene(app->scene_manager, AppSceneSdExplorer);
            consumed = true;
            break;
        case SdMenuFormat:
            scene_manager_next_scene(app->scene_manager, AppSceneSdFormat);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void app_scene_sd_card_on_exit(void* ctx) {
    App* app = ctx;
    submenu_reset(app->submenu);
}
