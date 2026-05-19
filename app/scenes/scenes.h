#pragma once

#include <gui/scene_manager.h>

typedef enum {
    AppSceneBoot,
    AppSceneMainMenu,
    AppSceneLed,
    AppSceneCount,
} AppScene;

extern const SceneManagerHandlers app_scene_handlers;

/* Boot scene */
void app_scene_boot_on_enter(void* ctx);
bool app_scene_boot_on_event(void* ctx, SceneManagerEvent event);
void app_scene_boot_on_exit(void* ctx);

/* LED scene */
void app_scene_led_on_enter(void* ctx);
bool app_scene_led_on_event(void* ctx, SceneManagerEvent event);
void app_scene_led_on_exit(void* ctx);

/* Main menu scene */
void app_scene_main_menu_on_enter(void* ctx);
bool app_scene_main_menu_on_event(void* ctx, SceneManagerEvent event);
void app_scene_main_menu_on_exit(void* ctx);
