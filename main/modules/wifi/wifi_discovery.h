#pragma once

#include <stddef.h>

#include "esp_err.h"

#define WIFI_DISCOVERY_ERR_BASE 0x7100
#define WIFI_DISCOVERY_ERR_BUSY (WIFI_DISCOVERY_ERR_BASE + 1)
#define WIFI_DISCOVERY_ERR_IP_INFO_FAILED (WIFI_DISCOVERY_ERR_BASE + 2)

typedef void (*wifi_discovery_result_writer_t)(const char *line, void *context);

esp_err_t wifi_discovery_scan_subnet(wifi_discovery_result_writer_t write_line, void *context);