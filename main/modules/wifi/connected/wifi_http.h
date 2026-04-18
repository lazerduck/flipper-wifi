#pragma once

#include "esp_err.h"

typedef void (*wifi_http_result_writer_t)(const char *line, void *context);

esp_err_t wifi_http_fetch_preset(
    const char *preset_name,
    wifi_http_result_writer_t write_line,
    void *context);