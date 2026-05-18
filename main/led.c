#include "led.h"
#include "led_strip.h"

void led_init(void)
{
    // TODO: configure RMT channel for WS2812 on CONFIG_FUSE_LED_PIN via led_strip driver
}

void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    // TODO: write RGB value to the led_strip handle and call led_strip_refresh()
}

void led_set_mode(app_mode_t mode)
{
    // TODO: map mode to its idle colour from the table in Architecture/Overview.md and call led_set()
}

void led_pulse_boot(void)
{
    // TODO: fade white in and out once (128,128,128) to signal successful boot
}

void led_blink_start(void)
{
    // TODO: spawn a FreeRTOS task that toggles the current colour on/off at ~2 Hz
}

void led_blink_stop(void)
{
    // TODO: delete the blink task and restore the solid current colour
}
