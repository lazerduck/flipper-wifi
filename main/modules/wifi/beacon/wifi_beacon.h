#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Safety limits */
#define WIFI_BEACON_MAX_SSIDS           8U
#define WIFI_BEACON_MAX_SSID_LENGTH     32U
#define WIFI_BEACON_MAX_DURATION_MS     60000U
#define WIFI_BEACON_INTERVAL_MS         102U  /* 100 TU, standard beacon period */
#define WIFI_BEACON_TX_POWER_MAX        8     /* ~8 dBm — roughly half of full power */

#define WIFI_BEACON_ERR_BASE            0x7300
#define WIFI_BEACON_ERR_BUSY            (WIFI_BEACON_ERR_BASE + 1)
#define WIFI_BEACON_ERR_NOT_ACTIVE      (WIFI_BEACON_ERR_BASE + 2)

typedef void (*wifi_beacon_result_writer_t)(const char *line, void *context);

typedef struct {
    char ssids[WIFI_BEACON_MAX_SSIDS][WIFI_BEACON_MAX_SSID_LENGTH + 1U];
    size_t ssid_count;
    uint8_t channel;
    uint32_t duration_ms;
} wifi_beacon_config_t;

esp_err_t wifi_beacon_start(
    const wifi_beacon_config_t *config,
    wifi_beacon_result_writer_t write_line,
    void *context);
esp_err_t wifi_beacon_stop(void);
bool wifi_beacon_is_active(void);
