#pragma once

#include <stdint.h>
#include "types.h"

/*
 * led.h — WS2812 RGB LED driver.
 *
 * Colour meanings are documented in Architecture/Overview.md.
 * led_set_mode() picks the right idle colour for the current app mode.
 * led_blink_start/stop() are used whenever a long-running operation is
 * active — the LED blinks at the current mode colour to show activity.
 * The Flipper can override the colour at any time via LED_SET; the next
 * led_set_mode() call restores firmware control.
 */

/* Initialise the RMT-backed WS2812 driver. */
void led_init(void);

/* Set the LED to an exact RGB value immediately. */
void led_set(uint8_t r, uint8_t g, uint8_t b);

/* Set the LED to the idle colour for the given app mode. */
void led_set_mode(app_mode_t mode);

/* Play a short white pulse — used once during boot. */
void led_pulse_boot(void);

/* Start blinking the current colour (scanning / long-running op active). */
void led_blink_start(void);

/* Stop blinking and return to a solid colour. */
void led_blink_stop(void);
