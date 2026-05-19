#pragma once

#include <gui/scene_manager.h>

typedef enum {
    AppSceneBoot,
    AppSceneMainMenu,
    AppSceneLed,
    AppSceneSdCard,
    AppSceneSdDetails,
    AppSceneSdExplorer,
    AppSceneSdFileAction,
    AppSceneSdFormat,
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

/* SD Card scenes */
void app_scene_sd_card_on_enter(void* ctx);
bool app_scene_sd_card_on_event(void* ctx, SceneManagerEvent event);
void app_scene_sd_card_on_exit(void* ctx);

void app_scene_sd_details_on_enter(void* ctx);
bool app_scene_sd_details_on_event(void* ctx, SceneManagerEvent event);
void app_scene_sd_details_on_exit(void* ctx);

void app_scene_sd_explorer_on_enter(void* ctx);
bool app_scene_sd_explorer_on_event(void* ctx, SceneManagerEvent event);
void app_scene_sd_explorer_on_exit(void* ctx);

void app_scene_sd_file_action_on_enter(void* ctx);
bool app_scene_sd_file_action_on_event(void* ctx, SceneManagerEvent event);
void app_scene_sd_file_action_on_exit(void* ctx);

void app_scene_sd_format_on_enter(void* ctx);
bool app_scene_sd_format_on_event(void* ctx, SceneManagerEvent event);
void app_scene_sd_format_on_exit(void* ctx);
