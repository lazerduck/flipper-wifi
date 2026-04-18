#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_PROMISCUOUS_ERR_BASE 0x7200
#define WIFI_PROMISCUOUS_ERR_BUSY (WIFI_PROMISCUOUS_ERR_BASE + 1)
#define WIFI_PROMISCUOUS_ERR_NOT_ACTIVE (WIFI_PROMISCUOUS_ERR_BASE + 2)
#define WIFI_PROMISCUOUS_WATCH_REPORT_INTERVAL_MS 1000U

#define WIFI_PROMISCUOUS_MAX_CHANNELS 14U

typedef void (*wifi_promiscuous_result_writer_t)(const char *line, void *context);

typedef struct {
    uint8_t channels[WIFI_PROMISCUOUS_MAX_CHANNELS];
    size_t channel_count;
    uint32_t dwell_ms;
    int8_t rssi_min;
} wifi_promiscuous_survey_config_t;

typedef struct {
    uint8_t channel;
    uint32_t report_interval_ms;
    int8_t rssi_min;
} wifi_promiscuous_watch_config_t;

esp_err_t wifi_promiscuous_survey(
    const wifi_promiscuous_survey_config_t *config,
    wifi_promiscuous_result_writer_t write_line,
    void *context);
esp_err_t wifi_promiscuous_watch_start(
    const wifi_promiscuous_watch_config_t *config,
    wifi_promiscuous_result_writer_t write_line,
    void *context);
esp_err_t wifi_promiscuous_watch_stop(void);
bool wifi_promiscuous_watch_is_active(void);