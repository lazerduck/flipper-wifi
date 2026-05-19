#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/dialog_ex.h>
#include <gui/view.h>

#include "uart.h"
#include "scenes/scenes.h"

/* How long to wait after enabling 5V before sending PING (ESP boot time). */
#define APP_ESP_INIT_DELAY_MS 1500u
/* How long to wait for a PONG response before counting it as a miss. */
#define APP_PING_TIMEOUT_MS   2000u
/* Number of PING attempts before giving up and showing the error screen. */
#define APP_BOOT_MAX_RETRIES  3u

typedef enum {
    AppViewSubmenu,
    AppViewPopup,
    AppViewDialogEx,
    AppViewLed,
    AppViewSdDetails,
    AppViewSdExplorer,
} AppView;

typedef enum {
    BootStateIdle,
    BootStateWaitEsp,   /* waiting for ESP init delay */
    BootStateWaitPong,  /* PING sent, waiting for PONG */
    BootStateFailed,
} BootState;

/* ── SD card entry (one row in a directory listing) ────────────────────── */
#define SD_MAX_ENTRIES 64u

typedef struct {
    char     name[64];
    uint32_t size_bytes;
    bool     is_dir;
} SdEntry;

/* Two-step format confirmation state */
typedef enum {
    SdFormatIdle,
    SdFormatConfirm1,
    SdFormatConfirm2,
    SdFormatRunning,
    SdFormatDone,
    SdFormatFailed,
} SdFormatStep;

typedef struct {
    SceneManager*   scene_manager;
    ViewDispatcher* view_dispatcher;
    Submenu*        submenu;
    Popup*          popup;
    DialogEx*       dialog_ex;
    AppUart*        uart;        /* allocated during boot, lives for app lifetime */
    FuriTimer*      boot_timer;  /* owned by boot scene, freed on exit */
    BootState       boot_state;
    uint8_t         boot_retries;
    /* LED scene state */
    View*           led_view;
    uint8_t         led_r, led_g, led_b;   /* 0-240, step 16 */
    uint8_t         led_channel;           /* 0=R 1=G 2=B    */
    bool            led_on;
    /* SD Card scenes */
    View*           sd_details_view;
    View*           sd_explorer_view;
    /* SD status (filled by SD_STATUS / SD_REMOUNT response) */
    bool            sd_present;
    bool            sd_mounted;
    uint32_t        sd_free_kb;
    uint32_t        sd_total_kb;
    char            sd_fs_type[8];
    /* SD explorer state */
    char            sd_path[256];          /* current directory, always starts with "/" */
    char            sd_action_path[320];   /* full path of the file selected for action */
    SdEntry         sd_entries[SD_MAX_ENTRIES];
    uint16_t        sd_entry_count;
    uint16_t        sd_entry_selected;
    uint16_t        sd_scroll_offset;
    bool            sd_stream_done;        /* true once END is received */
    /* SD format state */
    SdFormatStep    sd_format_step;
} App;

App* app_alloc(void);
void app_free(App* app);
