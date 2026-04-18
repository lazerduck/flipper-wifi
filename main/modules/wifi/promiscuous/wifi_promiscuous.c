#include "modules/wifi/promiscuous/wifi_promiscuous.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "local/esp_wifi_types_native.h"

#define WIFI_PROMISCUOUS_DEFAULT_RSSI_MIN -95
#define WIFI_PROMISCUOUS_LINE_MAX_LENGTH 224
#define WIFI_PROMISCUOUS_MAX_UNIQUE_TRANSMITTERS 64U

typedef enum {
    WIFI_PROMISCUOUS_CAPTURE_NONE = 0,
    WIFI_PROMISCUOUS_CAPTURE_SURVEY,
    WIFI_PROMISCUOUS_CAPTURE_WATCH,
} wifi_promiscuous_capture_mode_t;

typedef struct {
    uint8_t channel;
    uint32_t total_frames;
    uint32_t management_frames;
    uint32_t control_frames;
    uint32_t data_frames;
    uint32_t misc_frames;
    uint32_t beacon_frames;
    uint32_t deauth_frames;
    uint8_t unique_transmitter_count;
    bool has_rssi;
    int8_t strongest_rssi;
    uint8_t transmitters[WIFI_PROMISCUOUS_MAX_UNIQUE_TRANSMITTERS][6];
} wifi_promiscuous_stats_t;

static portMUX_TYPE s_promiscuous_guard = portMUX_INITIALIZER_UNLOCKED;
static bool s_capture_active = false;
static int8_t s_rssi_min = WIFI_PROMISCUOUS_DEFAULT_RSSI_MIN;
static wifi_promiscuous_stats_t *s_active_stats = NULL;

static const wifi_pkt_rx_ctrl_t *wifi_promiscuous_extract_rx_ctrl(
    const void *buffer,
    wifi_promiscuous_pkt_type_t type)
{
    if (buffer == NULL) {
        return NULL;
    }

    return &((const wifi_promiscuous_pkt_t *)buffer)->rx_ctrl;
}

static const uint8_t *wifi_promiscuous_extract_payload(
    const void *buffer,
    wifi_promiscuous_pkt_type_t type)
{
    if (buffer == NULL || type == WIFI_PKT_MISC) {
        return NULL;
    }

    return ((const wifi_promiscuous_pkt_t *)buffer)->payload;
}

static uint16_t wifi_promiscuous_extract_length(
    const wifi_pkt_rx_ctrl_t *rx_ctrl,
    wifi_promiscuous_pkt_type_t type)
{
    if (rx_ctrl == NULL || type == WIFI_PKT_MISC) {
        return 0U;
    }

    return (uint16_t)rx_ctrl->sig_len;
}

static void wifi_promiscuous_reset_stats(wifi_promiscuous_stats_t *stats, uint8_t channel)
{
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    stats->channel = channel;
}

static bool wifi_promiscuous_mac_seen(
    const wifi_promiscuous_stats_t *stats,
    const uint8_t *address)
{
    uint8_t index;

    if (stats == NULL || address == NULL) {
        return true;
    }

    for (index = 0; index < stats->unique_transmitter_count; ++index) {
        if (memcmp(stats->transmitters[index], address, 6U) == 0) {
            return true;
        }
    }

    return false;
}

static void wifi_promiscuous_track_mac(wifi_promiscuous_stats_t *stats, const uint8_t *address)
{
    if (stats == NULL || address == NULL) {
        return;
    }

    if (stats->unique_transmitter_count >= WIFI_PROMISCUOUS_MAX_UNIQUE_TRANSMITTERS) {
        return;
    }

    if (wifi_promiscuous_mac_seen(stats, address)) {
        return;
    }

    memcpy(stats->transmitters[stats->unique_transmitter_count], address, 6U);
    ++stats->unique_transmitter_count;
}

static void wifi_promiscuous_capture_callback(void *buffer, wifi_promiscuous_pkt_type_t type)
{
    const wifi_pkt_rx_ctrl_t *rx_ctrl;
    const uint8_t *payload;
    uint16_t payload_length;

    rx_ctrl = wifi_promiscuous_extract_rx_ctrl(buffer, type);
    if (rx_ctrl == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_promiscuous_guard);

    if (!s_capture_active || s_active_stats == NULL) {
        portEXIT_CRITICAL(&s_promiscuous_guard);
        return;
    }

    if ((int8_t)rx_ctrl->rssi < s_rssi_min || rx_ctrl->rx_state != 0U) {
        portEXIT_CRITICAL(&s_promiscuous_guard);
        return;
    }

    payload = wifi_promiscuous_extract_payload(buffer, type);
    payload_length = wifi_promiscuous_extract_length(rx_ctrl, type);

    ++s_active_stats->total_frames;
    if (!s_active_stats->has_rssi || (int8_t)rx_ctrl->rssi > s_active_stats->strongest_rssi) {
        s_active_stats->has_rssi = true;
        s_active_stats->strongest_rssi = (int8_t)rx_ctrl->rssi;
    }

    switch (type) {
    case WIFI_PKT_MGMT:
        ++s_active_stats->management_frames;
        if (payload != NULL && payload_length >= 16U) {
            wifi_promiscuous_track_mac(s_active_stats, &payload[10]);
        }
        if (payload != NULL && payload_length >= 24U) {
            uint8_t subtype = (uint8_t)(payload[0] >> 4U);

            if (subtype == 8U) {
                ++s_active_stats->beacon_frames;
            } else if (subtype == 12U) {
                ++s_active_stats->deauth_frames;
            }
        }
        break;
    case WIFI_PKT_CTRL:
        ++s_active_stats->control_frames;
        break;
    case WIFI_PKT_DATA:
        ++s_active_stats->data_frames;
        if (payload != NULL && payload_length >= 16U) {
            wifi_promiscuous_track_mac(s_active_stats, &payload[10]);
        }
        break;
    case WIFI_PKT_MISC:
    default:
        ++s_active_stats->misc_frames;
        break;
    }

    portEXIT_CRITICAL(&s_promiscuous_guard);
}

static esp_err_t wifi_promiscuous_begin_capture(
    wifi_promiscuous_capture_mode_t mode,
    wifi_promiscuous_stats_t *stats,
    int8_t rssi_min)
{
    esp_err_t err;

    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_promiscuous_guard);
    if (s_capture_active) {
        portEXIT_CRITICAL(&s_promiscuous_guard);
        return WIFI_PROMISCUOUS_ERR_BUSY;
    }

    s_capture_active = true;
    s_rssi_min = rssi_min;
    s_active_stats = stats;
    portEXIT_CRITICAL(&s_promiscuous_guard);

    err = esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_capture_callback);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_promiscuous_guard);
        s_capture_active = false;
        s_active_stats = NULL;
        portEXIT_CRITICAL(&s_promiscuous_guard);
    }

    return err;
}

static void wifi_promiscuous_end_capture(void)
{
    esp_wifi_set_promiscuous_rx_cb(NULL);

    portENTER_CRITICAL(&s_promiscuous_guard);
    s_capture_active = false;
    s_active_stats = NULL;
    portEXIT_CRITICAL(&s_promiscuous_guard);
}

static void wifi_promiscuous_write_stats_line(
    const char *prefix,
    const wifi_promiscuous_stats_t *stats,
    uint32_t duration_ms,
    wifi_promiscuous_result_writer_t write_line,
    void *context)
{
    char line[WIFI_PROMISCUOUS_LINE_MAX_LENGTH];

    if (prefix == NULL || stats == NULL || write_line == NULL) {
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "%s channel=%u total=%u mgmt=%u data=%u ctrl=%u misc=%u beacons=%u deauth=%u unique=%u max_rssi=%d duration_ms=%u\n",
        prefix,
        (unsigned int)stats->channel,
        (unsigned int)stats->total_frames,
        (unsigned int)stats->management_frames,
        (unsigned int)stats->data_frames,
        (unsigned int)stats->control_frames,
        (unsigned int)stats->misc_frames,
        (unsigned int)stats->beacon_frames,
        (unsigned int)stats->deauth_frames,
        (unsigned int)stats->unique_transmitter_count,
        stats->has_rssi ? (int)stats->strongest_rssi : -127,
        (unsigned int)duration_ms);
    write_line(line, context);
}

esp_err_t wifi_promiscuous_survey(
    const wifi_promiscuous_survey_config_t *config,
    wifi_promiscuous_result_writer_t write_line,
    void *context)
{
    wifi_promiscuous_stats_t stats[WIFI_PROMISCUOUS_MAX_CHANNELS];
    uint32_t duration_ms;
    uint8_t recommended_channel = 0U;
    uint32_t recommended_total = UINT32_MAX;
    int64_t started_at_us;
    size_t index;
    esp_err_t err;

    if (config == NULL || config->channel_count == 0U ||
        config->channel_count > WIFI_PROMISCUOUS_MAX_CHANNELS ||
        config->dwell_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(stats));
    started_at_us = esp_timer_get_time();

    for (index = 0; index < config->channel_count; ++index) {
        wifi_promiscuous_reset_stats(&stats[index], config->channels[index]);

        err = wifi_promiscuous_begin_capture(
            WIFI_PROMISCUOUS_CAPTURE_SURVEY,
            &stats[index],
            config->rssi_min);
        if (err != ESP_OK) {
            return err;
        }

        err = esp_wifi_set_channel(config->channels[index], WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) {
            wifi_promiscuous_end_capture();
            return err;
        }

        vTaskDelay(pdMS_TO_TICKS(config->dwell_ms));
        wifi_promiscuous_end_capture();

        if (stats[index].total_frames < recommended_total) {
            recommended_total = stats[index].total_frames;
            recommended_channel = stats[index].channel;
        }

        wifi_promiscuous_write_stats_line(
            "SURVEY",
            &stats[index],
            config->dwell_ms,
            write_line,
            context);
    }

    duration_ms = (uint32_t)((esp_timer_get_time() - started_at_us) / 1000LL);
    if (write_line != NULL) {
        char line[WIFI_PROMISCUOUS_LINE_MAX_LENGTH];

        snprintf(
            line,
            sizeof(line),
            "SURVEY_DONE channels=%u dwell_ms=%u duration_ms=%u recommended=%u\n",
            (unsigned int)config->channel_count,
            (unsigned int)config->dwell_ms,
            (unsigned int)duration_ms,
            (unsigned int)recommended_channel);
        write_line(line, context);
    }

    return ESP_OK;
}

esp_err_t wifi_promiscuous_watch(
    const wifi_promiscuous_watch_config_t *config,
    wifi_promiscuous_result_writer_t write_line,
    void *context)
{
    wifi_promiscuous_stats_t stats;
    uint32_t duration_ms;
    int64_t started_at_us;
    esp_err_t err;

    if (config == NULL || config->channel == 0U || config->channel > 14U ||
        config->duration_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_promiscuous_reset_stats(&stats, config->channel);

    err = wifi_promiscuous_begin_capture(
        WIFI_PROMISCUOUS_CAPTURE_WATCH,
        &stats,
        config->rssi_min);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_channel(config->channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        wifi_promiscuous_end_capture();
        return err;
    }

    started_at_us = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(config->duration_ms));
    wifi_promiscuous_end_capture();

    duration_ms = (uint32_t)((esp_timer_get_time() - started_at_us) / 1000LL);
    wifi_promiscuous_write_stats_line("WATCH", &stats, duration_ms, write_line, context);

    if (write_line != NULL) {
        char line[WIFI_PROMISCUOUS_LINE_MAX_LENGTH];

        snprintf(
            line,
            sizeof(line),
            "WATCH_DONE channel=%u duration_ms=%u\n",
            (unsigned int)config->channel,
            (unsigned int)duration_ms);
        write_line(line, context);
    }

    return ESP_OK;
}