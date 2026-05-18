#include "types.h"
#include "sd_card.h"
#include "settings.h"
#include "led.h"
#include "uart.h"

void app_main(void)
{
    app_state_t state = {
        .mode      = APP_MODE_IDLE,
        .active_op = NULL,
    };

    /* 1. SD card — attempt mount; boot continues whether or not the card is present */
    sd_init(&state.sd);

    /* 2. Settings — load from SD if mounted, otherwise apply built-in defaults */
    settings_init(&state.sd);

    /* 3. LED — init WS2812 driver, pulse white to signal a clean boot */
    led_init();
    led_pulse_boot();

    /* 4. UART — start the line-reader task; commands are accepted from this point */
    uart_init(&state);

    /* 5. Ready — dim white idle colour; the UART task drives everything from here */
    led_set_mode(APP_MODE_IDLE);
}

