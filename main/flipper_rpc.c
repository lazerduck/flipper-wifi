#include "flipper_rpc.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "flipper_rpc";

#define FLIPPER_PROTO_WIRE_VARINT 0U
#define FLIPPER_PROTO_WIRE_LEN 2U

static size_t flipper_rpc_encode_varint(uint32_t value, uint8_t *buffer, size_t buffer_size)
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

static esp_err_t flipper_rpc_decode_varint(
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
        if(shift >= 32) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t flipper_rpc_skip_field(
    uint32_t wire_type,
    const uint8_t *buffer,
    size_t buffer_size,
    size_t *offset)
{
    uint32_t field_size = 0;

    if(wire_type == FLIPPER_PROTO_WIRE_VARINT) {
        return flipper_rpc_decode_varint(buffer, buffer_size, offset, &field_size);
    }

    if(wire_type != FLIPPER_PROTO_WIRE_LEN) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_RETURN_ON_ERROR(flipper_rpc_decode_varint(buffer, buffer_size, offset, &field_size), TAG, "field size decode failed");

    if((*offset + field_size) > buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    *offset += field_size;
    return ESP_OK;
}

static esp_err_t flipper_rpc_wait_for_status(flipper_transport_t *transport, uint32_t timeout_ms)
{
    const int64_t deadline_ms = flipper_transport_now_ms() + timeout_ms;

    while(flipper_transport_now_ms() < deadline_ms) {
        flipper_frame_t frame;
        const esp_err_t result = flipper_transport_receive_frame(transport, &frame, 50);

        if(result == ESP_OK) {
            if(frame.header == FLIPPER_FRAME_STATUS) {
                return (frame.payload[0] == FLIPPER_STATUS_OK) ? ESP_OK : ESP_FAIL;
            }

            if(frame.header == FLIPPER_FRAME_HEARTBEAT) {
                ESP_RETURN_ON_ERROR(flipper_transport_send_heartbeat(transport), TAG, "heartbeat reply failed");
                continue;
            }

            ESP_LOGW(TAG, "unexpected frame 0x%02X while waiting for DATA ack", frame.header);
            return ESP_ERR_INVALID_RESPONSE;
        }

        if(result != ESP_ERR_TIMEOUT) {
            return result;
        }

        if(flipper_transport_heartbeat_due(transport)) {
            ESP_RETURN_ON_ERROR(flipper_transport_send_heartbeat(transport), TAG, "heartbeat send failed");
        }

        if(flipper_transport_link_timed_out(transport)) {
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t flipper_rpc_send_bytes(flipper_transport_t *transport, const uint8_t *data, size_t data_size)
{
    size_t offset = 0;

    while(offset < data_size) {
        const size_t chunk_size = ((data_size - offset) > FLIPPER_MAX_DATA_SIZE) ?
            FLIPPER_MAX_DATA_SIZE :
            (data_size - offset);

        ESP_RETURN_ON_ERROR(
            flipper_transport_send_data(transport, data + offset, chunk_size),
            TAG,
            "data send failed");
        ESP_RETURN_ON_ERROR(flipper_rpc_wait_for_status(transport, FLIPPER_TIMEOUT_MS), TAG, "data send rejected");
        offset += chunk_size;
    }

    return ESP_OK;
}

static esp_err_t flipper_rpc_receive_message(
    flipper_transport_t *transport,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *message_size,
    uint32_t timeout_ms)
{
    size_t offset = 0;
    const int64_t deadline_ms = flipper_transport_now_ms() + timeout_ms;

    while(flipper_transport_now_ms() < deadline_ms) {
        flipper_frame_t frame;
        const esp_err_t result = flipper_transport_receive_frame(transport, &frame, 50);

        if(result == ESP_OK) {
            if(frame.header == FLIPPER_FRAME_HEARTBEAT) {
                ESP_RETURN_ON_ERROR(flipper_transport_send_heartbeat(transport), TAG, "heartbeat reply failed");
                continue;
            }

            if(frame.header != FLIPPER_FRAME_DATA) {
                ESP_LOGW(TAG, "unexpected frame 0x%02X while waiting for RPC response", frame.header);
                return ESP_ERR_INVALID_RESPONSE;
            }

            ESP_RETURN_ON_ERROR(flipper_transport_send_status(transport, FLIPPER_STATUS_OK), TAG, "response ack failed");

            const size_t chunk_size = frame.payload[0];
            if((offset + chunk_size) > buffer_size) {
                return ESP_ERR_NO_MEM;
            }

            memcpy(buffer + offset, &frame.payload[1], chunk_size);
            offset += chunk_size;

            size_t prefix_offset = 0;
            uint32_t encoded_size = 0;
            if(flipper_rpc_decode_varint(buffer, offset, &prefix_offset, &encoded_size) == ESP_OK) {
                const size_t full_size = prefix_offset + encoded_size;
                if(full_size <= offset) {
                    *message_size = full_size;
                    return ESP_OK;
                }
            }

            continue;
        }

        if(result != ESP_ERR_TIMEOUT) {
            return result;
        }

        if(flipper_transport_heartbeat_due(transport)) {
            ESP_RETURN_ON_ERROR(flipper_transport_send_heartbeat(transport), TAG, "heartbeat send failed");
        }

        if(flipper_transport_link_timed_out(transport)) {
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t flipper_rpc_build_message(
    uint32_t command_id,
    uint32_t content_tag,
    const uint8_t *content,
    size_t content_len,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size)
{
    uint8_t message[FLIPPER_RPC_MAX_MESSAGE_SIZE];
    size_t message_size = 0;

    const size_t command_key_size = flipper_rpc_encode_varint((1U << 3) | FLIPPER_PROTO_WIRE_VARINT, message + message_size, sizeof(message) - message_size);
    if(command_key_size == 0) {
        return ESP_ERR_NO_MEM;
    }
    message_size += command_key_size;

    const size_t command_id_size = flipper_rpc_encode_varint(command_id, message + message_size, sizeof(message) - message_size);
    if(command_id_size == 0) {
        return ESP_ERR_NO_MEM;
    }
    message_size += command_id_size;

    const size_t content_key_size = flipper_rpc_encode_varint((content_tag << 3) | FLIPPER_PROTO_WIRE_LEN, message + message_size, sizeof(message) - message_size);
    if(content_key_size == 0) {
        return ESP_ERR_NO_MEM;
    }
    message_size += content_key_size;

    const size_t content_length_size = flipper_rpc_encode_varint((uint32_t)content_len, message + message_size, sizeof(message) - message_size);
    if(content_length_size == 0) {
        return ESP_ERR_NO_MEM;
    }
    message_size += content_length_size;

    if((message_size + content_len) > sizeof(message)) {
        return ESP_ERR_NO_MEM;
    }

    if(content_len > 0) {
        memcpy(message + message_size, content, content_len);
        message_size += content_len;
    }

    const size_t prefix_size = flipper_rpc_encode_varint((uint32_t)message_size, buffer, buffer_size);
    if(prefix_size == 0 || (prefix_size + message_size) > buffer_size) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(buffer + prefix_size, message, message_size);
    *encoded_size = prefix_size + message_size;
    return ESP_OK;
}

esp_err_t flipper_rpc_parse_message(
    const uint8_t *data,
    size_t data_size,
    flipper_rpc_message_view_t *message_view)
{
    size_t offset = 0;
    uint32_t framed_size = 0;

    memset(message_view, 0, sizeof(*message_view));

    ESP_RETURN_ON_ERROR(flipper_rpc_decode_varint(data, data_size, &offset, &framed_size), TAG, "message size decode failed");
    if((offset + framed_size) > data_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t message_end = offset + framed_size;
    while(offset < message_end) {
        uint32_t key = 0;
        ESP_RETURN_ON_ERROR(flipper_rpc_decode_varint(data, message_end, &offset, &key), TAG, "field key decode failed");

        const uint32_t field_number = key >> 3;
        const uint32_t wire_type = key & 0x07U;

        if(field_number == 1U && wire_type == FLIPPER_PROTO_WIRE_VARINT) {
            ESP_RETURN_ON_ERROR(flipper_rpc_decode_varint(data, message_end, &offset, &message_view->command_id), TAG, "command id decode failed");
            continue;
        }

        if(field_number == 2U && wire_type == FLIPPER_PROTO_WIRE_VARINT) {
            ESP_RETURN_ON_ERROR(flipper_rpc_decode_varint(data, message_end, &offset, &message_view->command_status), TAG, "status decode failed");
            continue;
        }

        if(field_number == 3U && wire_type == FLIPPER_PROTO_WIRE_VARINT) {
            uint32_t has_next = 0;
            ESP_RETURN_ON_ERROR(flipper_rpc_decode_varint(data, message_end, &offset, &has_next), TAG, "has_next decode failed");
            message_view->has_next = has_next != 0;
            continue;
        }

        if(wire_type == FLIPPER_PROTO_WIRE_LEN && message_view->content_tag == 0U) {
            uint32_t field_size = 0;
            ESP_RETURN_ON_ERROR(flipper_rpc_decode_varint(data, message_end, &offset, &field_size), TAG, "content size decode failed");
            if((offset + field_size) > message_end) {
                return ESP_ERR_INVALID_SIZE;
            }
            message_view->content_tag = field_number;
            message_view->content = data + offset;
            message_view->content_len = field_size;
            offset += field_size;
            continue;
        }

        ESP_RETURN_ON_ERROR(flipper_rpc_skip_field(wire_type, data, message_end, &offset), TAG, "skip field failed");
    }

    return (message_view->content_tag != 0U) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t flipper_rpc_request(
    flipper_transport_t *transport,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_buffer_size,
    size_t *response_size,
    uint32_t timeout_ms)
{
    ESP_RETURN_ON_ERROR(flipper_rpc_send_bytes(transport, request, request_size), TAG, "rpc request send failed");
    return flipper_rpc_receive_message(transport, response, response_buffer_size, response_size, timeout_ms);
}

esp_err_t flipper_rpc_idle(flipper_transport_t *transport, uint32_t duration_ms)
{
    const int64_t deadline_ms = flipper_transport_now_ms() + duration_ms;

    while(flipper_transport_now_ms() < deadline_ms) {
        flipper_frame_t frame;
        const uint32_t step_ms = (uint32_t)((deadline_ms - flipper_transport_now_ms()) > 50 ? 50 : (deadline_ms - flipper_transport_now_ms()));
        const esp_err_t result = flipper_transport_receive_frame(transport, &frame, step_ms);

        if(result == ESP_OK) {
            if(frame.header == FLIPPER_FRAME_HEARTBEAT) {
                ESP_RETURN_ON_ERROR(flipper_transport_send_heartbeat(transport), TAG, "heartbeat reply failed");
                continue;
            }

            if(frame.header == FLIPPER_FRAME_STATUS) {
                continue;
            }

            ESP_LOGW(TAG, "unexpected frame 0x%02X while idle", frame.header);
            return ESP_ERR_INVALID_RESPONSE;
        }

        if(result != ESP_ERR_TIMEOUT) {
            return result;
        }

        if(flipper_transport_heartbeat_due(transport)) {
            ESP_RETURN_ON_ERROR(flipper_transport_send_heartbeat(transport), TAG, "heartbeat send failed");
        }

        if(flipper_transport_link_timed_out(transport)) {
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_OK;
}