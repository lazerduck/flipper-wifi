#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define ZIGBEE_BDB_STATUS_NO_NETWORK 0x03U

typedef struct {
    bool joined;
    uint16_t pan_id;
    uint8_t channel;
    uint16_t short_addr;
    uint64_t ext_pan_id;
} zigbee_stack_network_info_t;

typedef struct {
    bool got_bdb_response; /* false = timeout (radio may not be working) */
    bool joined;           /* true = found open network and joined it */
    uint8_t bdb_status;    /* raw BDB commissioning status code */
    zigbee_stack_network_info_t net; /* valid when joined=true */
} zigbee_stack_scan_result_t;

esp_err_t zigbee_stack_init(void);

esp_err_t zigbee_stack_join(uint8_t channel, uint32_t timeout_ms, zigbee_stack_network_info_t* out_info);
esp_err_t zigbee_stack_leave(uint32_t timeout_ms);
esp_err_t zigbee_stack_scan(uint8_t channel, zigbee_stack_scan_result_t* out);
esp_err_t zigbee_stack_get_network_info(zigbee_stack_network_info_t* out_info);
esp_err_t zigbee_stack_trigger_button(uint8_t button_index);
