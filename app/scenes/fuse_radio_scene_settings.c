#include "../fuse_radio_app_i.h"

#include <stdio.h>
#include <string.h>

/* Per-item callback — context carries the entry index */
typedef struct {
    FuseRadioApp* app;
    uint8_t       entry_idx;
} FuseRadioSettingsItemCtx;

/* We allocate these in a small static pool — the list is rebuilt on each enter */
#define SETTINGS_CTX_POOL_SIZE FUSE_RADIO_MAX_CONFIG_ENTRIES
static FuseRadioSettingsItemCtx s_item_ctx_pool[SETTINGS_CTX_POOL_SIZE];

static void fuse_radio_scene_settings_bool_changed(VariableItem* item) {
    FuseRadioSettingsItemCtx* ctx =
        (FuseRadioSettingsItemCtx*)variable_item_get_context(item);
    if(ctx == NULL || ctx->app == NULL) {
        return;
    }

    FuseRadioApp* app = ctx->app;
    uint8_t       idx = ctx->entry_idx;

    if(idx >= app->config_entry_count) {
        return;
    }

    uint8_t value_index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, value_index ? "ON" : "OFF");

    app->config_entries[idx].bool_value = (value_index != 0U);

    /* Send CONFIG SET command */
    fuse_radio_app_start_config_set(app, app->config_entries[idx].key, value_index != 0U);
}

/* ---------------------------------------------------------------------------
 * Build (or rebuild) the VariableItemList from current config_entries[]
 * --------------------------------------------------------------------------- */

static void fuse_radio_scene_settings_build_list(FuseRadioApp* app) {
    variable_item_list_reset(app->variable_item_list);

    for(uint8_t i = 0U; i < app->config_entry_count; i++) {
        if(i >= SETTINGS_CTX_POOL_SIZE) {
            break;
        }

        s_item_ctx_pool[i].app       = app;
        s_item_ctx_pool[i].entry_idx = i;

        if(app->config_entries[i].type == 0U) {
            /* Bool */
            VariableItem* item = variable_item_list_add(
                app->variable_item_list,
                app->config_entries[i].label,
                2U, /* OFF / ON */
                fuse_radio_scene_settings_bool_changed,
                &s_item_ctx_pool[i]);

            uint8_t cur = app->config_entries[i].bool_value ? 1U : 0U;
            variable_item_set_current_value_index(item, cur);
            variable_item_set_current_value_text(item, cur ? "ON" : "OFF");
        }
        /* Integer and future types handled here when added */
    }
}

/* ---------------------------------------------------------------------------
 * Scene handlers
 * --------------------------------------------------------------------------- */

void fuse_radio_scene_settings_on_enter(void* context) {
    FuseRadioApp* app = context;

    /* Show loading widget immediately */
    widget_reset(app->widget);
    widget_add_string_multiline_element(
        app->widget, 64, 32, AlignCenter, AlignCenter, FontSecondary, "Loading settings...");
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);

    /* Request config from the ESP */
    if(!fuse_radio_app_start_config_get(app)) {
        widget_reset(app->widget);
        widget_add_string_multiline_element(
            app->widget,
            64,
            32,
            AlignCenter,
            AlignCenter,
            FontSecondary,
            "Board not ready.");
        view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
    }
}

bool fuse_radio_scene_settings_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) {
        if(event.type == SceneManagerEventTypeBack) {
            return scene_manager_previous_scene(app->scene_manager);
        }
        return false;
    }

    if(event.event == FuseRadioCustomEventConfigLoaded) {
        if(app->config_entry_count == 0U) {
            /* Nothing to show */
            widget_reset(app->widget);
            widget_add_string_multiline_element(
                app->widget,
                64,
                32,
                AlignCenter,
                AlignCenter,
                FontSecondary,
                "No settings available.");
            if(!app->config_sd_available) {
                widget_add_string_multiline_element(
                    app->widget,
                    64,
                    50,
                    AlignCenter,
                    AlignCenter,
                    FontSecondary,
                    "No SD card - read only.");
            }
            view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
            return true;
        }

        fuse_radio_scene_settings_build_list(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewVariableItemList);
        return true;
    }

    if(event.event == FuseRadioCustomEventConfigNoSd) {
        /* CONFIG_DONE was not yet received for a GET — but it will come after.
         * The sd_available flag is already cleared; the list will still render. */
        return true;
    }

    if(event.event == FuseRadioCustomEventConfigSetDone) {
        /* A CONFIG SET succeeded — no UI change needed (toggle already updated) */
        return true;
    }

    if(event.event == FuseRadioCustomEventConfigFailed) {
        widget_reset(app->widget);

        if(!app->config_sd_available) {
            widget_add_string_multiline_element(
                app->widget,
                64,
                28,
                AlignCenter,
                AlignCenter,
                FontPrimary,
                "No SD card available");
            widget_add_string_multiline_element(
                app->widget,
                64,
                46,
                AlignCenter,
                AlignCenter,
                FontSecondary,
                "Settings cannot be saved.");
        } else {
            widget_add_string_multiline_element(
                app->widget,
                64,
                32,
                AlignCenter,
                AlignCenter,
                FontSecondary,
                app->config_info_text[0] != '\0' ? app->config_info_text :
                                                    "Settings error.");
        }

        view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
        return true;
    }

    return false;
}

void fuse_radio_scene_settings_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
    variable_item_list_reset(app->variable_item_list);
}
