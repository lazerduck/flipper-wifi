#include "modules/status_led/status_led.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_common.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

#define STATUS_LED_GPIO GPIO_NUM_8
#define STATUS_LED_RMT_RESOLUTION_HZ 10000000
#define STATUS_LED_MEM_BLOCK_SYMBOLS 64
#define STATUS_LED_QUEUE_DEPTH 1
#define STATUS_LED_TX_TIMEOUT_MS 50

typedef struct {
    uint8_t green;
    uint8_t red;
    uint8_t blue;
} ws2812_pixel_t;

static bool s_status_led_initialized = false;
static rmt_channel_handle_t s_status_led_channel = NULL;
static rmt_encoder_handle_t s_status_led_encoder = NULL;

static esp_err_t status_led_write_pixel(ws2812_pixel_t pixel)
{
    const rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0,
            .queue_nonblocking = 0,
        },
    };
    esp_err_t err;

    if (!s_status_led_initialized || s_status_led_channel == NULL || s_status_led_encoder == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    err = rmt_transmit(s_status_led_channel, s_status_led_encoder, &pixel, sizeof(pixel), &tx_config);
    if (err != ESP_OK) {
        return err;
    }

    return rmt_tx_wait_all_done(s_status_led_channel, STATUS_LED_TX_TIMEOUT_MS);
}

esp_err_t status_led_init(void)
{
    const rmt_tx_channel_config_t tx_channel_config = {
        .gpio_num = STATUS_LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = STATUS_LED_RMT_RESOLUTION_HZ,
        .mem_block_symbols = STATUS_LED_MEM_BLOCK_SYMBOLS,
        .trans_queue_depth = STATUS_LED_QUEUE_DEPTH,
        .intr_priority = 0,
        .flags = {
            .invert_out = 0,
            .with_dma = 0,
            .allow_pd = 0,
            .init_level = 0,
        },
    };
    const rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 4,
            .level1 = 0,
            .duration1 = 8,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 8,
            .level1 = 0,
            .duration1 = 4,
        },
        .flags = {
            .msb_first = 1,
        },
    };
    esp_err_t err;

    if (s_status_led_initialized) {
        return ESP_OK;
    }

    err = rmt_new_tx_channel(&tx_channel_config, &s_status_led_channel);
    if (err != ESP_OK) {
        return err;
    }

    err = rmt_new_bytes_encoder(&bytes_encoder_config, &s_status_led_encoder);
    if (err != ESP_OK) {
        rmt_del_channel(s_status_led_channel);
        s_status_led_channel = NULL;
        return err;
    }

    err = rmt_enable(s_status_led_channel);
    if (err != ESP_OK) {
        rmt_del_encoder(s_status_led_encoder);
        rmt_del_channel(s_status_led_channel);
        s_status_led_encoder = NULL;
        s_status_led_channel = NULL;
        return err;
    }

    s_status_led_initialized = true;
    return status_led_apply_wifi_status(&(const wifi_manager_status_t){
        .mode = WIFI_MANAGER_MODE_IDLE,
        .action = WIFI_MANAGER_ACTION_NONE,
    });
}

bool status_led_is_ready(void)
{
    return s_status_led_initialized;
}

esp_err_t status_led_apply_wifi_status(const wifi_manager_status_t *status)
{
    ws2812_pixel_t pixel = {
        .green = 3,
        .red = 3,
        .blue = 3,
    };

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (status->action) {
    case WIFI_MANAGER_ACTION_SCANNING:
        pixel.green = 12;
        pixel.red = 16;
        pixel.blue = 0;
        break;
    case WIFI_MANAGER_ACTION_CONNECTING:
    case WIFI_MANAGER_ACTION_DISCONNECTING:
        pixel.green = 8;
        pixel.red = 6;
        pixel.blue = 0;
        break;
    default:
        break;
    }

    switch (status->mode) {
    case WIFI_MANAGER_MODE_CONNECTED:
        pixel.green = 16;
        pixel.red = 0;
        pixel.blue = 0;
        break;
    case WIFI_MANAGER_MODE_PROMISCUOUS:
        pixel.green = 0;
        pixel.red = 0;
        pixel.blue = 16;
        break;
    case WIFI_MANAGER_MODE_IDLE:
    default:
        break;
    }

    return status_led_write_pixel(pixel);
}