#include "app.h"

static bool app_custom_event_cb(void* ctx, uint32_t event) {
    App* app = ctx;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool app_back_event_cb(void* ctx) {
    App* app = ctx;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void app_tick_cb(void* ctx) {
    App* app = ctx;
    scene_manager_handle_tick_event(app->scene_manager);
}

App* app_alloc(void) {
    App* app = malloc(sizeof(App));

    app->scene_manager = scene_manager_alloc(&app_scene_handlers, app);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, app_custom_event_cb);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, app_back_event_cb);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, app_tick_cb, 100);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AppViewSubmenu, submenu_get_view(app->submenu));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AppViewPopup, popup_get_view(app->popup));

    app->dialog_ex = dialog_ex_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, AppViewDialogEx, dialog_ex_get_view(app->dialog_ex));

    app->led_view = view_alloc();
    view_allocate_model(app->led_view, ViewModelTypeLocking, sizeof(App*));
    view_dispatcher_add_view(app->view_dispatcher, AppViewLed, app->led_view);

    app->sd_details_view = view_alloc();
    view_allocate_model(app->sd_details_view, ViewModelTypeLocking, sizeof(App*));
    view_dispatcher_add_view(app->view_dispatcher, AppViewSdDetails, app->sd_details_view);

    app->sd_explorer_view = view_alloc();
    view_allocate_model(app->sd_explorer_view, ViewModelTypeLocking, sizeof(App*));
    view_dispatcher_add_view(app->view_dispatcher, AppViewSdExplorer, app->sd_explorer_view);

    app->uart         = NULL;
    app->boot_timer   = NULL;
    app->boot_state   = BootStateIdle;
    app->boot_retries = 0;
    app->led_r        = 0;
    app->led_g        = 0;
    app->led_b        = 0;
    app->led_channel  = 0;
    app->led_on       = true;

    app->sd_present        = false;
    app->sd_mounted        = false;
    app->sd_free_kb        = 0;
    app->sd_total_kb       = 0;
    app->sd_fs_type[0]     = '\0';
    app->sd_path[0]        = '/';
    app->sd_path[1]        = '\0';
    app->sd_action_path[0] = '\0';
    app->sd_entry_count    = 0;
    app->sd_entry_selected = 0;
    app->sd_scroll_offset  = 0;
    app->sd_stream_done    = false;
    app->sd_format_step    = SdFormatIdle;

    return app;
}

void app_free(App* app) {
    view_dispatcher_remove_view(app->view_dispatcher, AppViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, AppViewPopup);
    view_dispatcher_remove_view(app->view_dispatcher, AppViewDialogEx);
    view_dispatcher_remove_view(app->view_dispatcher, AppViewLed);
    view_dispatcher_remove_view(app->view_dispatcher, AppViewSdDetails);
    view_dispatcher_remove_view(app->view_dispatcher, AppViewSdExplorer);
    submenu_free(app->submenu);
    popup_free(app->popup);
    dialog_ex_free(app->dialog_ex);
    view_free(app->led_view);
    view_free(app->sd_details_view);
    view_free(app->sd_explorer_view);
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    if(app->uart) app_uart_free(app->uart);
    free(app);
}

int32_t fuse_radio_app(void* p) {
    UNUSED(p);

    App* app = app_alloc();
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);

    scene_manager_next_scene(app->scene_manager, AppSceneBoot);
    view_dispatcher_run(app->view_dispatcher);

    /* Disable 5V expansion rail on exit regardless of how we got here */
    if(furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
    }

    furi_record_close(RECORD_GUI);
    app_free(app);
    return 0;
}
