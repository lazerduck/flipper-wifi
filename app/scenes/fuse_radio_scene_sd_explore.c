#include "../fuse_radio_app_i.h"

#include <stdio.h>
#include <string.h>

#define FUSE_RADIO_SD_EXPLORE_EVENT_UP 1000U
#define FUSE_RADIO_SD_EXPLORE_EVENT_EMPTY 1001U

static void fuse_radio_scene_sd_explore_parent(char* path, size_t path_size) {
    char* slash;

    if(path == NULL || path_size == 0U || path[0] == '\0' || strcmp(path, "/") == 0) {
        if(path != NULL && path_size > 1U) {
            snprintf(path, path_size, "/");
        }
        return;
    }

    slash = strrchr(path, '/');
    if(slash == NULL || slash == path) {
        snprintf(path, path_size, "/");
        return;
    }

    *slash = '\0';
}

static void fuse_radio_scene_sd_explore_join(
    const char* base,
    const char* entry,
    char* out,
    size_t out_size) {
    size_t pos = 0U;
    size_t base_len;
    size_t entry_len;

    if(base == NULL || entry == NULL || out == NULL || out_size == 0U) {
        return;
    }

    out[0] = '\0';

    if(strcmp(base, "/") == 0) {
        if(out_size < 3U) {
            return;
        }

        out[0] = '/';
        pos = 1U;
    } else {
        base_len = strlen(base);
        if(base_len >= out_size) {
            base_len = out_size - 1U;
        }

        if(base_len > 0U) {
            memcpy(out, base, base_len);
            pos = base_len;
        }

        if(pos > 0U && out[pos - 1U] != '/' && pos < out_size - 1U) {
            out[pos++] = '/';
        }
    }

    entry_len = strlen(entry);
    for(size_t i = 0; i < entry_len && pos < out_size - 1U; i++) {
        out[pos++] = entry[i];
    }

    out[pos] = '\0';
}

void fuse_radio_scene_sd_explore_on_enter(void* context) {
    FuseRadioApp* app = context;

    if(app->sd_explore_path[0] == '\0') {
        snprintf(app->sd_explore_path, sizeof(app->sd_explore_path), "/");
    }

    app->sd_action = FuseRadioSdActionExplore;
    app->sd_entry_count = 0U;
    fuse_radio_app_start_sd_action(app, FuseRadioSdActionExplore);

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, "SD Explore");
    widget_add_string_multiline_element(
        app->widget,
        64,
        30,
        AlignCenter,
        AlignCenter,
        FontSecondary,
        "Loading directory...");
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_sd_explore_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == FuseRadioCustomEventSdRefresh) {
            app->sd_action = FuseRadioSdActionExplore;
            app->sd_entry_count = 0U;
            fuse_radio_app_start_sd_action(app, FuseRadioSdActionExplore);
            return true;
        }

        if(event.event == FUSE_RADIO_SD_EXPLORE_EVENT_UP) {
            fuse_radio_scene_sd_explore_parent(app->sd_explore_path, sizeof(app->sd_explore_path));
            app->sd_action = FuseRadioSdActionExplore;
            app->sd_entry_count = 0U;
            fuse_radio_app_start_sd_action(app, FuseRadioSdActionExplore);
            return true;
        }

        if(event.event == FUSE_RADIO_SD_EXPLORE_EVENT_EMPTY) {
            return true;
        }

        if(event.event < app->sd_entry_count) {
            FuseRadioSdEntry* entry = &app->sd_entries[event.event];

            if(entry->is_dir) {
                char next_path[FUSE_RADIO_SD_PATH_SIZE];
                fuse_radio_scene_sd_explore_join(
                    app->sd_explore_path, entry->name, next_path, sizeof(next_path));
                snprintf(app->sd_explore_path, sizeof(app->sd_explore_path), "%s", next_path);
                app->sd_action = FuseRadioSdActionExplore;
                app->sd_entry_count = 0U;
                fuse_radio_app_start_sd_action(app, FuseRadioSdActionExplore);
            }

            return true;
        }
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_sd_explore_on_exit(void* context) {
    FuseRadioApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
