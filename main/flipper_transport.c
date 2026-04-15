#include "flipper_transport.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "flipper_transport";

static uint8_t flipper_transport_checksum(uint8_t header, const uint8_t *payload, size_t payload_len)
{
    uint8_t checksum = header;

    for(size_t index = 0; index < payload_len; ++index) {
        checksum ^= payload[index];
    }

    return checksum;
}

int64_t flipper_transport_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

bool flipper_transport_heartbeat_due(const flipper_transport_t *transport)
{
    return (flipper_transport_now_ms() - transport->last_tx_ms) >= FLIPPER_HEARTBEAT_INTERVAL_MS;
}

bool flipper_transport_link_timed_out(const flipper_transport_t *transport)
{
    return (flipper_transport_now_ms() - transport->last_rx_ms) >= FLIPPER_TIMEOUT_MS;
}

static esp_err_t flipper_transport_uart_init(uint32_t baud_rate)
{
    static bool driver_installed = false;

    const uart_config_t config = {
        .baud_rate = (int)baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(FLIPPER_UART_PORT, &config));
    ESP_ERROR_CHECK(uart_set_pin(
        FLIPPER_UART_PORT,
        FLIPPER_UART_TX_PIN,
        FLIPPER_UART_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE));

    if(!driver_installed) {
        ESP_ERROR_CHECK(uart_driver_install(
            FLIPPER_UART_PORT,
            FLIPPER_UART_BUFFER_SIZE,
            FLIPPER_UART_BUFFER_SIZE,
            0,
            NULL,
            0));
        driver_installed = true;
    } else {
        ESP_ERROR_CHECK(uart_set_baudrate(FLIPPER_UART_PORT, baud_rate));
        ESP_ERROR_CHECK(uart_flush(FLIPPER_UART_PORT));
    }

    return ESP_OK;
}

static void flipper_transport_signal_presence(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << FLIPPER_UART_TX_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(FLIPPER_UART_TX_PIN, 0));
    vTaskDelay(pdMS_TO_TICKS(FLIPPER_DETECT_PULSE_MS));
}

static esp_err_t flipper_transport_read_exact(uint8_t *buffer, size_t length, uint32_t timeout_ms)
{
    size_t offset = 0;
    const int64_t deadline_ms = flipper_transport_now_ms() + timeout_ms;

    while(offset < length) {
        const int64_t now_ms = flipper_transport_now_ms();

        if(now_ms >= deadline_ms) {
            return ESP_ERR_TIMEOUT;
        }

        const uint32_t remaining_ms = (uint32_t)(deadline_ms - now_ms);
        const int read_count = uart_read_bytes(
            FLIPPER_UART_PORT,
            buffer + offset,
            length - offset,
            pdMS_TO_TICKS(remaining_ms));

        if(read_count < 0) {
            return ESP_FAIL;
        }

        if(read_count == 0) {
            continue;
        }

        offset += (size_t)read_count;
    }

    return ESP_OK;
}

static esp_err_t flipper_transport_send_frame(
    flipper_transport_t *transport,
    uint8_t header,
    const uint8_t *payload,
    size_t payload_len)
{
    const uint8_t checksum = flipper_transport_checksum(header, payload, payload_len);

    if(uart_write_bytes(FLIPPER_UART_PORT, &header, 1) != 1) {
        return ESP_FAIL;
    }

    if(payload_len > 0 && uart_write_bytes(FLIPPER_UART_PORT, payload, payload_len) != (int)payload_len) {
        return ESP_FAIL;
    }

    if(uart_write_bytes(FLIPPER_UART_PORT, &checksum, 1) != 1) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(
        uart_wait_tx_done(FLIPPER_UART_PORT, pdMS_TO_TICKS(FLIPPER_TIMEOUT_MS)),
        TAG,
        "tx wait failed");

    transport->last_tx_ms = flipper_transport_now_ms();
    return ESP_OK;
}

static esp_err_t flipper_transport_send_baudrate(flipper_transport_t *transport, uint32_t baud_rate)
{
    const uint8_t payload[4] = {
        (uint8_t)(baud_rate & 0xFF),
        (uint8_t)((baud_rate >> 8) & 0xFF),
        (uint8_t)((baud_rate >> 16) & 0xFF),
        (uint8_t)((baud_rate >> 24) & 0xFF),
    };

    return flipper_transport_send_frame(transport, FLIPPER_FRAME_BAUDRATE, payload, sizeof(payload));
}

static esp_err_t flipper_transport_send_control(flipper_transport_t *transport, uint8_t command)
{
    return flipper_transport_send_frame(transport, FLIPPER_FRAME_CONTROL, &command, 1);
}

static esp_err_t flipper_transport_wait_for_status(
    flipper_transport_t *transport,
    uint8_t expected_status,
    uint32_t timeout_ms)
{
    flipper_frame_t frame;

    ESP_RETURN_ON_ERROR(
        flipper_transport_receive_frame(transport, &frame, timeout_ms),
        TAG,
        "status receive failed");

    if(frame.header != FLIPPER_FRAME_STATUS) {
        ESP_LOGW(TAG, "expected STATUS, got 0x%02X", frame.header);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if(frame.payload[0] != expected_status) {
        ESP_LOGW(TAG, "unexpected status 0x%02X", frame.payload[0]);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t flipper_transport_open_rpc(flipper_transport_t *transport)
{
    flipper_frame_t frame;

    memset(transport, 0, sizeof(*transport));

    ESP_RETURN_ON_ERROR(flipper_transport_uart_init(FLIPPER_DETECT_BAUD), TAG, "uart init failed");
    flipper_transport_signal_presence();
    ESP_RETURN_ON_ERROR(flipper_transport_uart_init(FLIPPER_DETECT_BAUD), TAG, "uart restore failed");

    ESP_LOGI(TAG, "waiting for Flipper heartbeat at %u baud", FLIPPER_DETECT_BAUD);
    ESP_RETURN_ON_ERROR(
        flipper_transport_receive_frame(transport, &frame, FLIPPER_TIMEOUT_MS),
        TAG,
        "initial heartbeat timeout");

    if(frame.header != FLIPPER_FRAME_HEARTBEAT) {
        ESP_LOGW(TAG, "expected HEARTBEAT, got 0x%02X", frame.header);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_RETURN_ON_ERROR(flipper_transport_send_baudrate(transport, FLIPPER_TARGET_BAUD), TAG, "baud request failed");
    ESP_RETURN_ON_ERROR(
        flipper_transport_wait_for_status(transport, FLIPPER_STATUS_OK, FLIPPER_TIMEOUT_MS),
        TAG,
        "baud request rejected");

    ESP_ERROR_CHECK(uart_set_baudrate(FLIPPER_UART_PORT, FLIPPER_TARGET_BAUD));
    vTaskDelay(pdMS_TO_TICKS(FLIPPER_BAUD_SWITCH_DEADTIME_MS));

    ESP_RETURN_ON_ERROR(
        flipper_transport_send_control(transport, FLIPPER_CONTROL_START_RPC),
        TAG,
        "start rpc failed");
    ESP_RETURN_ON_ERROR(
        flipper_transport_wait_for_status(transport, FLIPPER_STATUS_OK, FLIPPER_TIMEOUT_MS),
        TAG,
        "rpc start rejected");

    ESP_LOGI(TAG, "Flipper RPC link established at %u baud", FLIPPER_TARGET_BAUD);
    return ESP_OK;
}

esp_err_t flipper_transport_receive_frame(flipper_transport_t *transport, flipper_frame_t *frame, uint32_t timeout_ms)
{
    uint8_t checksum = 0;

    memset(frame, 0, sizeof(*frame));
    ESP_RETURN_ON_ERROR(flipper_transport_read_exact(&frame->header, 1, timeout_ms), TAG, "header read failed");

    switch(frame->header) {
    case FLIPPER_FRAME_HEARTBEAT:
        frame->payload_len = 0;
        break;
    case FLIPPER_FRAME_STATUS:
    case FLIPPER_FRAME_CONTROL:
        frame->payload_len = 1;
        ESP_RETURN_ON_ERROR(
            flipper_transport_read_exact(frame->payload, frame->payload_len, timeout_ms),
            TAG,
            "payload read failed");
        break;
    case FLIPPER_FRAME_BAUDRATE:
        frame->payload_len = 4;
        ESP_RETURN_ON_ERROR(
            flipper_transport_read_exact(frame->payload, frame->payload_len, timeout_ms),
            TAG,
            "payload read failed");
        break;
    case FLIPPER_FRAME_DATA:
        ESP_RETURN_ON_ERROR(flipper_transport_read_exact(frame->payload, 1, timeout_ms), TAG, "data size read failed");
        frame->payload_len = 1 + frame->payload[0];
        if(frame->payload[0] > FLIPPER_MAX_DATA_SIZE) {
            return ESP_ERR_INVALID_SIZE;
        }
        if(frame->payload[0] > 0) {
            ESP_RETURN_ON_ERROR(
                flipper_transport_read_exact(&frame->payload[1], frame->payload[0], timeout_ms),
                TAG,
                "data read failed");
        }
        break;
    default:
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_RETURN_ON_ERROR(flipper_transport_read_exact(&checksum, 1, timeout_ms), TAG, "checksum read failed");

    if(checksum != flipper_transport_checksum(frame->header, frame->payload, frame->payload_len)) {
        return ESP_ERR_INVALID_CRC;
    }

    transport->last_rx_ms = flipper_transport_now_ms();
    return ESP_OK;
}

esp_err_t flipper_transport_send_status(flipper_transport_t *transport, uint8_t status_code)
{
    return flipper_transport_send_frame(transport, FLIPPER_FRAME_STATUS, &status_code, 1);
}

esp_err_t flipper_transport_send_heartbeat(flipper_transport_t *transport)
{
    return flipper_transport_send_frame(transport, FLIPPER_FRAME_HEARTBEAT, NULL, 0);
}

esp_err_t flipper_transport_send_data(flipper_transport_t *transport, const uint8_t *data, size_t data_size)
{
    uint8_t payload[1 + FLIPPER_MAX_DATA_SIZE];

    if(data_size > FLIPPER_MAX_DATA_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    payload[0] = (uint8_t)data_size;
    if(data_size > 0) {
        memcpy(&payload[1], data, data_size);
    }

    return flipper_transport_send_frame(transport, FLIPPER_FRAME_DATA, payload, data_size + 1);
}

void flipper_transport_reset(void)
{
    uart_flush(FLIPPER_UART_PORT);
}