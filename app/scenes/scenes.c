#include "scenes.h"

static void (*const on_enter_handlers[AppSceneCount])(void*) = {
    [AppSceneBoot]     = app_scene_boot_on_enter,
    [AppSceneMainMenu] = app_scene_main_menu_on_enter,
    [AppSceneLed]      = app_scene_led_on_enter,
};

static bool (*const on_event_handlers[AppSceneCount])(void*, SceneManagerEvent) = {
    [AppSceneBoot]     = app_scene_boot_on_event,
    [AppSceneMainMenu] = app_scene_main_menu_on_event,
    [AppSceneLed]      = app_scene_led_on_event,
};

static void (*const on_exit_handlers[AppSceneCount])(void*) = {
    [AppSceneBoot]     = app_scene_boot_on_exit,
    [AppSceneMainMenu] = app_scene_main_menu_on_exit,
    [AppSceneLed]      = app_scene_led_on_exit,
};

const SceneManagerHandlers app_scene_handlers = {
    .on_enter_handlers = on_enter_handlers,
    .on_event_handlers = on_event_handlers,
    .on_exit_handlers  = on_exit_handlers,
    .scene_num         = AppSceneCount,
};
