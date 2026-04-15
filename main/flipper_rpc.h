#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "flipper_transport.h"

#define FLIPPER_RPC_MAX_MESSAGE_SIZE 256U

#define FLIPPER_RPC_STATUS_OK 0U

#define FLIPPER_RPC_CONTENT_EMPTY 4U
#define FLIPPER_RPC_CONTENT_SYSTEM_PING_REQUEST 5U
#define FLIPPER_RPC_CONTENT_SYSTEM_PING_RESPONSE 6U

typedef struct {
    uint32_t command_id;
    uint32_t command_status;
    bool has_next;
    uint32_t content_tag;
    const uint8_t *content;
    size_t content_len;
} flipper_rpc_message_view_t;

esp_err_t flipper_rpc_build_message(
    uint32_t command_id,
    uint32_t content_tag,
    const uint8_t *content,
    size_t content_len,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size);

esp_err_t flipper_rpc_parse_message(
    const uint8_t *data,
    size_t data_size,
    flipper_rpc_message_view_t *message_view);

esp_err_t flipper_rpc_request(
    flipper_transport_t *transport,
    const uint8_t *request,
    size_t request_size,
    uint8_t *response,
    size_t response_buffer_size,
    size_t *response_size,
    uint32_t timeout_ms);

esp_err_t flipper_rpc_idle(flipper_transport_t *transport, uint32_t duration_ms);