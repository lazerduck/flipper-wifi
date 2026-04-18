#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef void (*wifi_mdns_result_writer_t)(const char *line, void *context);

esp_err_t wifi_mdns_query_hostname(const char *host, wifi_mdns_result_writer_t write_line, void *context);
