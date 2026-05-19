#include "../../app.h"
#include "sd_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SdExplorerEventListDone,
    SdExplorerEventListError,
} SdExplorerEvent;

#define EXPLORER_ROWS 5u
#define EXPLORER_ROW_H 10u

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void sd_explorer_fetch(App* app) {
    app->sd_entry_count    = 0;
    app->sd_entry_selected = 0;
    app->sd_scroll_offset  = 0;
    app->sd_stream_done    = false;
    /* Show "Loading..." immediately */
    with_view_model(app->sd_explorer_view, App** model, { (void)model; }, true);
    char cmd[280];
    snprintf(cmd, sizeof(cmd), "SD_LIST path=\"%s\"\n", app->sd_path);
    app_uart_send(app->uart, cmd);
}

static void sd_clamp_scroll(App* app) {
    if(app->sd_entry_count == 0) {
        app->sd_scroll_offset = 0;
        return;
    }
    if(app->sd_entry_selected >= app->sd_entry_count)
        app->sd_entry_selected = app->sd_entry_count - 1;
    if(app->sd_entry_selected < app->sd_scroll_offset)
        app->sd_scroll_offset = app->sd_entry_selected;
    if(app->sd_entry_selected >= app->sd_scroll_offset + EXPLORER_ROWS)
        app->sd_scroll_offset = app->sd_entry_selected - EXPLORER_ROWS + 1;
}

/* ── UART callback (worker thread context) ───────────────────────────────── */

static void sd_explorer_uart_cb(const char* line, void* ctx) {
    App* app = ctx;

    if(strcmp(line, "S STREAM") == 0) return; /* stream starts — already reset */

    if(strcmp(line, "END") == 0 || strcmp(line, "STOPPED") == 0) {
        app->sd_stream_done = true;
        view_dispatcher_send_custom_event(app->view_dispatcher, SdExplorerEventListDone);
        return;
    }
    if(strncmp(line, "E ", 2) == 0) {
        app->sd_stream_done = true;
        view_dispatcher_send_custom_event(app->view_dispatcher, SdExplorerEventListError);
        return;
    }
    if(strncmp(line, "DATA ", 5) == 0 && app->sd_entry_count < SD_MAX_ENTRIES) {
        SdEntry* e       = &app->sd_entries[app->sd_entry_count];
        const char* data = line + 5;
        char val[32];
        sd_parse_field(data, "name", e->name, sizeof(e->name));
        if(sd_parse_field(data, "size", val, sizeof(val)))
            e->size_bytes = (uint32_t)atoi(val);
        e->is_dir = false;
        if(sd_parse_field(data, "dir", val, sizeof(val)))
            e->is_dir = (strcmp(val, "true") == 0);
        app->sd_entry_count++;
    }
}

/* ── Draw callback ───────────────────────────────────────────────────────── */

static void sd_explorer_draw_cb(Canvas* canvas, void* ctx) {
    App* app = *(App**)ctx;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    /* Header: current path (truncated to fit 128px) */
    const char* hdr = app->sd_path;
    char hdr_buf[22];
    if(strlen(hdr) > 21) {
        snprintf(hdr_buf, sizeof(hdr_buf), "...%s", hdr + strlen(hdr) - 18);
        hdr = hdr_buf;
    }
    canvas_draw_str(canvas, 2, 9, hdr);
    canvas_draw_line(canvas, 0, 11, 127, 11);

    if(!app->sd_stream_done) {
        canvas_draw_str(canvas, 32, 38, "Loading...");
        return;
    }
    if(app->sd_entry_count == 0) {
        canvas_draw_str(canvas, 20, 38, "Empty directory");
        return;
    }

    for(uint16_t i = 0; i < EXPLORER_ROWS; i++) {
        uint16_t idx = (uint16_t)(app->sd_scroll_offset + i);
        if(idx >= app->sd_entry_count) break;

        SdEntry* e    = &app->sd_entries[idx];
        uint8_t  row_top = (uint8_t)(12 + i * EXPLORER_ROW_H);
        uint8_t  row_bl  = (uint8_t)(row_top + EXPLORER_ROW_H - 1); /* baseline */
        bool     sel  = (idx == app->sd_entry_selected);

        if(sel) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, row_top, 128, EXPLORER_ROW_H);
            canvas_set_color(canvas, ColorWhite);
        }

        /* Name with directory indicator */
        char name_buf[20];
        if(e->is_dir) {
            snprintf(name_buf, sizeof(name_buf), "/%s", e->name);
        } else {
            snprintf(name_buf, sizeof(name_buf), "%s", e->name);
        }
        if(strlen(name_buf) > 18) {
            name_buf[15] = '.';
            name_buf[16] = '.';
            name_buf[17] = '.';
            name_buf[18] = '\0';
        }
        canvas_draw_str(canvas, 2, row_bl, name_buf);

        /* Size / type right-aligned */
        char size_buf[8];
        if(e->is_dir) {
            snprintf(size_buf, sizeof(size_buf), "[DIR]");
        } else if(e->size_bytes < 1024u) {
            snprintf(size_buf, sizeof(size_buf), "%ub", (unsigned)e->size_bytes);
        } else if(e->size_bytes < 1024u * 1024u) {
            snprintf(size_buf, sizeof(size_buf), "%uK", (unsigned)(e->size_bytes / 1024u));
        } else {
            snprintf(size_buf, sizeof(size_buf), "%uM", (unsigned)(e->size_bytes / (1024u * 1024u)));
        }
        /* Right-align: FontSecondary ~6px/char, leave 2px margin */
        uint8_t sz_x = (uint8_t)(126 - (uint8_t)(strlen(size_buf) * 6u));
        canvas_draw_str(canvas, sz_x, row_bl, size_buf);

        if(sel) canvas_set_color(canvas, ColorBlack);
    }

    /* Scroll indicator */
    if(app->sd_entry_count > EXPLORER_ROWS) {
        uint8_t track_h = (uint8_t)(EXPLORER_ROWS * EXPLORER_ROW_H);
        uint8_t thumb_h = (uint8_t)(track_h * EXPLORER_ROWS / app->sd_entry_count);
        if(thumb_h < 2) thumb_h = 2;
        uint8_t thumb_y = (uint8_t)(12 + track_h * app->sd_scroll_offset / app->sd_entry_count);
        canvas_draw_box(canvas, 126, thumb_y, 2, thumb_h);
    }
}

/* ── Input callback ──────────────────────────────────────────────────────── */

static bool sd_explorer_input_cb(InputEvent* event, void* ctx) {
    App* app = ctx;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(!app->sd_stream_done) return true; /* swallow input while loading */

    switch(event->key) {
    case InputKeyUp:
        if(app->sd_entry_count > 0 && app->sd_entry_selected > 0) {
            app->sd_entry_selected--;
            sd_clamp_scroll(app);
        }
        break;
    case InputKeyDown:
        if(app->sd_entry_count > 0 &&
           app->sd_entry_selected < app->sd_entry_count - 1) {
            app->sd_entry_selected++;
            sd_clamp_scroll(app);
        }
        break;
    case InputKeyRight:
    case InputKeyOk:
        if(app->sd_entry_count > 0 &&
           app->sd_entry_selected < app->sd_entry_count) {
            SdEntry* e = &app->sd_entries[app->sd_entry_selected];
            if(e->is_dir) {
                sd_path_enter(app->sd_path, sizeof(app->sd_path), e->name);
                sd_explorer_fetch(app);
            } else {
                /* Build full path for the file action scene */
                size_t plen = strlen(app->sd_path);
                if(plen > 1) {
                    snprintf(app->sd_action_path, sizeof(app->sd_action_path),
                             "%s/%s", app->sd_path, e->name);
                } else {
                    snprintf(app->sd_action_path, sizeof(app->sd_action_path),
                             "/%s", e->name);
                }
                scene_manager_next_scene(app->scene_manager, AppSceneSdFileAction);
            }
        }
        return true;
    default:
        return false;
    }

    with_view_model(app->sd_explorer_view, App** model, { (void)model; }, true);
    return true;
}

/* ── Scene callbacks ─────────────────────────────────────────────────────── */

void app_scene_sd_explorer_on_enter(void* ctx) {
    App* app = ctx;

    app_uart_set_callback(app->uart, sd_explorer_uart_cb, app);

    view_set_context(app->sd_explorer_view, app);
    view_set_draw_callback(app->sd_explorer_view, sd_explorer_draw_cb);
    view_set_input_callback(app->sd_explorer_view, sd_explorer_input_cb);
    with_view_model(app->sd_explorer_view, App** model, { *model = app; }, false);

    view_dispatcher_switch_to_view(app->view_dispatcher, AppViewSdExplorer);
    sd_explorer_fetch(app);
}

bool app_scene_sd_explorer_on_event(void* ctx, SceneManagerEvent event) {
    App* app = ctx;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {
        if(strcmp(app->sd_path, "/") != 0) {
            /* Navigate up rather than leaving the scene */
            sd_path_up(app->sd_path);
            sd_explorer_fetch(app);
            consumed = true;
        }
        /* else: let scene manager pop back to SD Card submenu */
    } else if(event.type == SceneManagerEventTypeCustom) {
        /* Redraw when list is complete or on error */
        with_view_model(app->sd_explorer_view, App** model, { (void)model; }, true);
        consumed = true;
    }
    return consumed;
}

void app_scene_sd_explorer_on_exit(void* ctx) {
    App* app = ctx;
    app_uart_set_callback(app->uart, NULL, NULL);
}
