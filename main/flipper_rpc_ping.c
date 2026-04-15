#include "flipper_rpc_ping.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "flipper_rpc.h"

static const char *TAG = "flipper_ping_demo";

#define FLIPPER_PING_INTERVAL_MS 1000U

static size_t flipper_ping_encode_varint(uint32_t value, uint8_t *buffer, size_t buffer_size)
{
    size_t offset = 0;

    do {
        if(offset >= buffer_size) {
            return 0;
        }

        uint8_t byte = value & 0x7FU;
        value >>= 7;
        if(value != 0) {
            byte |= 0x80U;
        }

        buffer[offset++] = byte;
    } while(value != 0);

    return offset;
}

static esp_err_t flipper_ping_decode_varint(
    const uint8_t *buffer,
    size_t buffer_size,
    size_t *offset,
    uint32_t *value)
{
    uint32_t result = 0;
    uint32_t shift = 0;

    while(*offset < buffer_size) {
        const uint8_t byte = buffer[(*offset)++];
        result |= (uint32_t)(byte & 0x7FU) << shift;

        if((byte & 0x80U) == 0) {
            *value = result;
            return ESP_OK;
        }

        shift += 7;
        if(shift >= 32U) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t flipper_ping_build_request(
    uint32_t command_id,
    const uint8_t *payload,
    size_t payload_size,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size)
{
    uint8_t content[1 + 5 + FLIPPER_MAX_DATA_SIZE];
    size_t content_size = 0;

    if(payload_size > FLIPPER_MAX_DATA_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    if(payload_size > 0) {
        content[content_size++] = 0x0AU;
        const size_t payload_length_size = flipper_ping_encode_varint((uint32_t)payload_size, content + content_size, sizeof(content) - content_size);
        if(payload_length_size == 0) {
            return ESP_ERR_NO_MEM;
        }
        content_size += payload_length_size;
        memcpy(content + content_size, payload, payload_size);
        content_size += payload_size;
    }

    return flipper_rpc_build_message(
        command_id,
        FLIPPER_RPC_CONTENT_SYSTEM_PING_REQUEST,
        content,
        content_size,
        buffer,
        buffer_size,
        encoded_size);
}

static esp_err_t flipper_ping_parse_response_payload(
    const uint8_t *content,
    size_t content_size,
    uint8_t *payload,
    size_t payload_buffer_size,
    size_t *payload_size)
{
    size_t offset = 0;

    *payload_size = 0;

    while(offset < content_size) {
        const uint8_t key = content[offset++];
        const uint32_t field_number = key >> 3;
        const uint32_t wire_type = key & 0x07U;

        if(field_number != 1U || wire_type != 2U) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        uint32_t bytes_size = 0;
        ESP_RETURN_ON_ERROR(flipper_ping_decode_varint(content, content_size, &offset, &bytes_size), TAG, "ping bytes length decode failed");
        if((offset + bytes_size) > content_size || bytes_size > payload_buffer_size) {
            return ESP_ERR_INVALID_SIZE;
        }

        memcpy(payload, content + offset, bytes_size);
        *payload_size = bytes_size;
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t flipper_rpc_ping_once(flipper_transport_t *transport, uint32_t command_id)
{
    uint8_t request[FLIPPER_RPC_MAX_MESSAGE_SIZE];
    uint8_t response[FLIPPER_RPC_MAX_MESSAGE_SIZE];
    uint8_t response_payload[FLIPPER_MAX_DATA_SIZE];
    char request_payload[32];
    size_t request_size = 0;
    size_t response_size = 0;
    size_t response_payload_size = 0;
    flipper_rpc_message_view_t response_view;

    const int written = snprintf(request_payload, sizeof(request_payload), "esp32-ping-%" PRIu32, command_id);
    if(written <= 0) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(
        flipper_ping_build_request(
            command_id,
            (const uint8_t *)request_payload,
            (size_t)written,
            request,
            sizeof(request),
            &request_size),
        TAG,
        "ping request encode failed");

    ESP_RETURN_ON_ERROR(
        flipper_rpc_request(
            transport,
            request,
            request_size,
            response,
            sizeof(response),
            &response_size,
            1000),
        TAG,
        "ping request failed");

    ESP_RETURN_ON_ERROR(flipper_rpc_parse_message(response, response_size, &response_view), TAG, "ping response parse failed");

    if(response_view.command_id != command_id) {
        ESP_LOGW(TAG, "unexpected ping response id: expected=%" PRIu32 " got=%" PRIu32, command_id, response_view.command_id);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if(response_view.command_status != FLIPPER_RPC_STATUS_OK) {
        ESP_LOGW(TAG, "ping response status=%" PRIu32, response_view.command_status);
        return ESP_FAIL;
    }

    if(response_view.content_tag != FLIPPER_RPC_CONTENT_SYSTEM_PING_RESPONSE) {
        ESP_LOGW(TAG, "unexpected ping content tag=%" PRIu32, response_view.content_tag);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_RETURN_ON_ERROR(
        flipper_ping_parse_response_payload(
            response_view.content,
            response_view.content_len,
            response_payload,
            sizeof(response_payload),
            &response_payload_size),
        TAG,
        "ping payload parse failed");

    if(response_payload_size != (size_t)written || memcmp(response_payload, request_payload, response_payload_size) != 0) {
        ESP_LOGW(TAG, "ping payload mismatch");
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "ping ok id=%" PRIu32 " payload=%s", command_id, request_payload);
    return ESP_OK;
}

void flipper_rpc_ping_demo_run(void)
{
    uint32_t command_id = 1;

    ESP_LOGI(TAG, "starting Flipper RPC ping demo");
    ESP_LOGI(TAG, "configure TX=%d RX=%d to match your ESP32-C6 wiring", FLIPPER_UART_TX_PIN, FLIPPER_UART_RX_PIN);

    while(true) {
        flipper_transport_t transport;
        const esp_err_t link_result = flipper_transport_open_rpc(&transport);

        if(link_result != ESP_OK) {
            ESP_LOGW(TAG, "link setup failed: %s", esp_err_to_name(link_result));
            flipper_transport_reset();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        while(true) {
            const esp_err_t ping_result = flipper_rpc_ping_once(&transport, command_id++);
            if(ping_result != ESP_OK) {
                ESP_LOGW(TAG, "ping failed: %s", esp_err_to_name(ping_result));
                break;
            }

            const esp_err_t idle_result = flipper_rpc_idle(&transport, FLIPPER_PING_INTERVAL_MS);
            if(idle_result != ESP_OK) {
                ESP_LOGW(TAG, "idle keepalive failed: %s", esp_err_to_name(idle_result));
                break;
            }
        }

        flipper_transport_reset();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}