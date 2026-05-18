#include "led.h"
#include "parser.h"
#include "uart.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

/* ── Module state ────────────────────────────────────────────────────────── */

static led_strip_handle_t s_strip;

/* Current solid colour — restored when blinking stops. */
static uint8_t s_cur_r, s_cur_g, s_cur_b;

/* Non-NULL while the blink task is running. */
static TaskHandle_t s_blink_task;

/* ── Idle colours (from Architecture/Overview.md) ────────────────────────── */

typedef struct { uint8_t r, g, b; } rgb_t;

static const rgb_t MODE_COLOURS[] = {
    [APP_MODE_IDLE]   = {  20,  20,  20 }, /* white dim  */
    [APP_MODE_WIFI]   = {   0, 180,   0 }, /* green      */
    [APP_MODE_BLE]    = {   0,   0, 255 }, /* blue       */
    [APP_MODE_ZIGBEE] = {   0, 220, 180 }, /* cyan       */
};

/* ── Internal helpers ────────────────────────────────────────────────────── */

static void blink_task(void *arg)
{
    (void)arg;
    bool on = true;
    while (1) {
        if (on) {
            led_strip_set_pixel(s_strip, 0, s_cur_r, s_cur_g, s_cur_b);
        } else {
            led_strip_clear(s_strip);
        }
        led_strip_refresh(s_strip);
        on = !on;
        vTaskDelay(pdMS_TO_TICKS(250)); /* 2 Hz: 250 ms on, 250 ms off */
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void led_init(void)
{
    led_strip_config_t cfg = {
        .strip_gpio_num          = CONFIG_FUSE_LED_PIN,
        .max_leds                = 1,
        .led_model               = LED_MODEL_WS2812,
        .color_component_format  = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000, /* 10 MHz */
    };
    led_strip_new_rmt_device(&cfg, &rmt_cfg, &s_strip);
    led_strip_clear(s_strip);
    led_strip_refresh(s_strip);
}

void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    s_cur_r = r; s_cur_g = g; s_cur_b = b;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

void led_set_mode(app_mode_t mode)
{
    if ((unsigned)mode >= (sizeof(MODE_COLOURS) / sizeof(MODE_COLOURS[0]))) return;
    const rgb_t *c = &MODE_COLOURS[mode];
    led_set(c->r, c->g, c->b);
}

void led_pulse_boot(void)
{
    /* Ramp up 0 → 128 then back down in steps of 8, 15 ms per step (~480 ms). */
    for (int v = 0; v <= 128; v += 8) {
        led_strip_set_pixel(s_strip, 0, v, v, v);
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    for (int v = 128; v >= 0; v -= 8) {
        led_strip_set_pixel(s_strip, 0, v, v, v);
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    /* Leave LED off; main.c will call led_set_mode(APP_MODE_IDLE) next. */
}

void led_blink_start(void)
{
    if (s_blink_task) return; /* already blinking */
    xTaskCreate(blink_task, "led_blink", 1024, NULL, 5, &s_blink_task);
}

void led_blink_stop(void)
{
    if (!s_blink_task) return;
    vTaskDelete(s_blink_task);
    s_blink_task = NULL;
    led_set(s_cur_r, s_cur_g, s_cur_b);
}

void led_handle_command(const parsed_cmd_t *cmd)
{
    /* Expects tokens[0]="LED" tokens[1]=sub-command */
    if (cmd->token_count < 2) { uart_send_error("ERR_UNKNOWN_CMD"); return; }
    const char *sub = cmd->tokens[1];

    /* LED_SET r="0-255" g="0-255" b="0-255" */
    if (strcmp(sub, "SET") == 0) {
        const char *r = parser_get_arg(cmd, "r");
        const char *g = parser_get_arg(cmd, "g");
        const char *b = parser_get_arg(cmd, "b");
        if (!r || !g || !b) { uart_send_error("ERR_MISSING_ARG"); return; }
        led_set((uint8_t)atoi(r), (uint8_t)atoi(g), (uint8_t)atoi(b));
        uart_send_ok();
        return;
    }

    uart_send_error("ERR_UNKNOWN_CMD");
}
