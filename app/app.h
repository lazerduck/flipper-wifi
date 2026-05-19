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
} AppView;

typedef enum {
    BootStateIdle,
    BootStateWaitEsp,   /* waiting for ESP init delay */
    BootStateWaitPong,  /* PING sent, waiting for PONG */
    BootStateFailed,
} BootState;

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
} App;

App* app_alloc(void);
void app_free(App* app);
