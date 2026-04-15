#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

#define FLIPPER_UART_PORT UART_NUM_1
#define FLIPPER_UART_TX_PIN GPIO_NUM_4
#define FLIPPER_UART_RX_PIN GPIO_NUM_5

#define FLIPPER_DETECT_BAUD 9600U
#define FLIPPER_TARGET_BAUD 115200U
#define FLIPPER_TIMEOUT_MS 250U
#define FLIPPER_BAUD_SWITCH_DEADTIME_MS 25U
#define FLIPPER_DETECT_PULSE_MS 5U
#define FLIPPER_HEARTBEAT_INTERVAL_MS 100U
#define FLIPPER_UART_BUFFER_SIZE 256U
#define FLIPPER_MAX_DATA_SIZE 64U

#define FLIPPER_FRAME_HEARTBEAT 0x01U
#define FLIPPER_FRAME_STATUS 0x02U
#define FLIPPER_FRAME_BAUDRATE 0x03U
#define FLIPPER_FRAME_CONTROL 0x04U
#define FLIPPER_FRAME_DATA 0x05U

#define FLIPPER_STATUS_OK 0x00U
#define FLIPPER_STATUS_ERROR_UNKNOWN 0x01U
#define FLIPPER_STATUS_ERROR_BAUD_NOT_SUPPORTED 0x02U

#define FLIPPER_CONTROL_START_RPC 0x00U
#define FLIPPER_CONTROL_STOP_RPC 0x01U

typedef struct {
    uint8_t header;
    uint8_t payload[1 + FLIPPER_MAX_DATA_SIZE];
    size_t payload_len;
} flipper_frame_t;

typedef struct {
    int64_t last_rx_ms;
    int64_t last_tx_ms;
} flipper_transport_t;

int64_t flipper_transport_now_ms(void);
bool flipper_transport_heartbeat_due(const flipper_transport_t *transport);
bool flipper_transport_link_timed_out(const flipper_transport_t *transport);

esp_err_t flipper_transport_open_rpc(flipper_transport_t *transport);
esp_err_t flipper_transport_receive_frame(flipper_transport_t *transport, flipper_frame_t *frame, uint32_t timeout_ms);
esp_err_t flipper_transport_send_status(flipper_transport_t *transport, uint8_t status_code);
esp_err_t flipper_transport_send_heartbeat(flipper_transport_t *transport);
esp_err_t flipper_transport_send_data(flipper_transport_t *transport, const uint8_t *data, size_t data_size);
void flipper_transport_reset(void);