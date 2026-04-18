#include "modules/wifi/promiscuous/wifi_promiscuous.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "local/esp_wifi_types_native.h"

#include "modules/wifi/wifi_manager.h"

#define WIFI_PROMISCUOUS_DEFAULT_RSSI_MIN -95
#define WIFI_PROMISCUOUS_LINE_MAX_LENGTH 224
#define WIFI_PROMISCUOUS_MAX_UNIQUE_TRANSMITTERS 64U
#define WIFI_PROMISCUOUS_WATCH_TASK_STACK_SIZE 4096U
#define WIFI_PROMISCUOUS_WATCH_TASK_POLL_MS 100U
#define WIFI_PROMISCUOUS_WATCH_MAX_REPORTED_DEVICES 12U

typedef enum {
    WIFI_PROMISCUOUS_CAPTURE_NONE = 0,
    WIFI_PROMISCUOUS_CAPTURE_SURVEY,
    WIFI_PROMISCUOUS_CAPTURE_WATCH,
} wifi_promiscuous_capture_mode_t;

typedef struct {
    uint8_t mac[6];
    uint32_t frames;
    uint16_t beacon_frames;
    int8_t strongest_rssi;
    bool has_rssi;
} wifi_promiscuous_transmitter_t;

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
    wifi_promiscuous_transmitter_t transmitters[WIFI_PROMISCUOUS_MAX_UNIQUE_TRANSMITTERS];
} wifi_promiscuous_stats_t;

typedef struct {
    bool active;
    bool stop_requested;
    TaskHandle_t task_handle;
    wifi_promiscuous_watch_config_t config;
    wifi_promiscuous_result_writer_t write_line;
    void *context;
    int64_t started_at_us;
    wifi_promiscuous_stats_t stats;
} wifi_promiscuous_watch_session_t;

static portMUX_TYPE s_promiscuous_guard = portMUX_INITIALIZER_UNLOCKED;
static bool s_capture_active = false;
static int8_t s_rssi_min = WIFI_PROMISCUOUS_DEFAULT_RSSI_MIN;
static wifi_promiscuous_stats_t *s_active_stats = NULL;
static wifi_promiscuous_watch_session_t s_watch_session;

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
    const uint8_t *address,
    uint8_t *existing_index)
{
    uint8_t index;

    if (stats == NULL || address == NULL) {
        return true;
    }

    for (index = 0; index < stats->unique_transmitter_count; ++index) {
        if (memcmp(stats->transmitters[index].mac, address, 6U) == 0) {
            if (existing_index != NULL) {
                *existing_index = index;
            }
            return true;
        }
    }

    return false;
}

static void wifi_promiscuous_track_mac(
    wifi_promiscuous_stats_t *stats,
    const uint8_t *address,
    int8_t rssi,
    bool is_beacon)
{
    uint8_t existing_index = 0U;

    if (stats == NULL || address == NULL) {
        return;
    }

    if (wifi_promiscuous_mac_seen(stats, address, &existing_index)) {
        ++stats->transmitters[existing_index].frames;
        if (!stats->transmitters[existing_index].has_rssi ||
            rssi > stats->transmitters[existing_index].strongest_rssi) {
            stats->transmitters[existing_index].has_rssi = true;
            stats->transmitters[existing_index].strongest_rssi = rssi;
        }
        if (is_beacon) {
            ++stats->transmitters[existing_index].beacon_frames;
        }
        return;
    }

    if (stats->unique_transmitter_count >= WIFI_PROMISCUOUS_MAX_UNIQUE_TRANSMITTERS) {
        return;
    }

    memcpy(stats->transmitters[stats->unique_transmitter_count].mac, address, 6U);
    stats->transmitters[stats->unique_transmitter_count].frames = 1U;
    stats->transmitters[stats->unique_transmitter_count].beacon_frames = is_beacon ? 1U : 0U;
    stats->transmitters[stats->unique_transmitter_count].strongest_rssi = rssi;
    stats->transmitters[stats->unique_transmitter_count].has_rssi = true;
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
        ++s_active_stats->management_frames; {
            bool is_beacon = false;

        if (payload != NULL && payload_length >= 16U) {
            if (payload != NULL && payload_length >= 24U) {
                uint8_t subtype = (uint8_t)(payload[0] >> 4U);

                if (subtype == 8U) {
                    ++s_active_stats->beacon_frames;
                    is_beacon = true;
                } else if (subtype == 12U) {
                    ++s_active_stats->deauth_frames;
                }
            }

            wifi_promiscuous_track_mac(
                s_active_stats,
                &payload[10],
                (int8_t)rx_ctrl->rssi,
                is_beacon);
        }
        }
        break;
    case WIFI_PKT_CTRL:
        ++s_active_stats->control_frames;
        break;
    case WIFI_PKT_DATA:
        ++s_active_stats->data_frames;
        if (payload != NULL && payload_length >= 16U) {
            wifi_promiscuous_track_mac(
                s_active_stats,
                &payload[10],
                (int8_t)rx_ctrl->rssi,
                false);
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

static void wifi_promiscuous_write_watch_live_line(
    const wifi_promiscuous_stats_t *stats,
    uint32_t elapsed_ms,
    wifi_promiscuous_result_writer_t write_line,
    void *context)
{
    char line[WIFI_PROMISCUOUS_LINE_MAX_LENGTH];

    if (stats == NULL || write_line == NULL) {
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "WATCH_LIVE channel=%u elapsed_ms=%u total=%u mgmt=%u data=%u ctrl=%u misc=%u beacons=%u deauth=%u unique=%u max_rssi=%d\n",
        (unsigned int)stats->channel,
        (unsigned int)elapsed_ms,
        (unsigned int)stats->total_frames,
        (unsigned int)stats->management_frames,
        (unsigned int)stats->data_frames,
        (unsigned int)stats->control_frames,
        (unsigned int)stats->misc_frames,
        (unsigned int)stats->beacon_frames,
        (unsigned int)stats->deauth_frames,
        (unsigned int)stats->unique_transmitter_count,
        stats->has_rssi ? (int)stats->strongest_rssi : -127);
    write_line(line, context);
}

static void wifi_promiscuous_write_watch_summary(
    const wifi_promiscuous_stats_t *stats,
    uint32_t duration_ms,
    wifi_promiscuous_result_writer_t write_line,
    void *context)
{
    char line[WIFI_PROMISCUOUS_LINE_MAX_LENGTH];
    bool emitted[WIFI_PROMISCUOUS_MAX_UNIQUE_TRANSMITTERS] = {false};
    uint8_t emitted_count = 0U;

    if (stats == NULL || write_line == NULL) {
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "WATCH_SUMMARY channel=%u duration_ms=%u total=%u mgmt=%u data=%u ctrl=%u misc=%u beacons=%u deauth=%u unique=%u max_rssi=%d\n",
        (unsigned int)stats->channel,
        (unsigned int)duration_ms,
        (unsigned int)stats->total_frames,
        (unsigned int)stats->management_frames,
        (unsigned int)stats->data_frames,
        (unsigned int)stats->control_frames,
        (unsigned int)stats->misc_frames,
        (unsigned int)stats->beacon_frames,
        (unsigned int)stats->deauth_frames,
        (unsigned int)stats->unique_transmitter_count,
        stats->has_rssi ? (int)stats->strongest_rssi : -127);
    write_line(line, context);

    while (emitted_count < stats->unique_transmitter_count &&
           emitted_count < WIFI_PROMISCUOUS_WATCH_MAX_REPORTED_DEVICES) {
        uint8_t best_index = WIFI_PROMISCUOUS_MAX_UNIQUE_TRANSMITTERS;

        for (uint8_t index = 0; index < stats->unique_transmitter_count; ++index) {
            if (emitted[index]) {
                continue;
            }

            if (best_index == WIFI_PROMISCUOUS_MAX_UNIQUE_TRANSMITTERS ||
                stats->transmitters[index].frames > stats->transmitters[best_index].frames) {
                best_index = index;
            }
        }

        if (best_index == WIFI_PROMISCUOUS_MAX_UNIQUE_TRANSMITTERS) {
            break;
        }

        emitted[best_index] = true;
        ++emitted_count;

        snprintf(
            line,
            sizeof(line),
            "WATCH_DEVICE mac=%02X:%02X:%02X:%02X:%02X:%02X frames=%u beacons=%u max_rssi=%d\n",
            stats->transmitters[best_index].mac[0],
            stats->transmitters[best_index].mac[1],
            stats->transmitters[best_index].mac[2],
            stats->transmitters[best_index].mac[3],
            stats->transmitters[best_index].mac[4],
            stats->transmitters[best_index].mac[5],
            (unsigned int)stats->transmitters[best_index].frames,
            (unsigned int)stats->transmitters[best_index].beacon_frames,
            stats->transmitters[best_index].has_rssi ?
                (int)stats->transmitters[best_index].strongest_rssi :
                -127);
        write_line(line, context);
    }
}

static void wifi_promiscuous_watch_copy_snapshot(
    wifi_promiscuous_stats_t *stats,
    bool *stop_requested,
    int64_t *started_at_us,
    wifi_promiscuous_result_writer_t *write_line,
    void **context,
    wifi_promiscuous_watch_config_t *config)
{
    portENTER_CRITICAL(&s_promiscuous_guard);
    if (stats != NULL) {
        *stats = s_watch_session.stats;
    }
    if (stop_requested != NULL) {
        *stop_requested = s_watch_session.stop_requested;
    }
    if (started_at_us != NULL) {
        *started_at_us = s_watch_session.started_at_us;
    }
    if (write_line != NULL) {
        *write_line = s_watch_session.write_line;
    }
    if (context != NULL) {
        *context = s_watch_session.context;
    }
    if (config != NULL) {
        *config = s_watch_session.config;
    }
    portEXIT_CRITICAL(&s_promiscuous_guard);
}

static void wifi_promiscuous_watch_finish(void)
{
    portENTER_CRITICAL(&s_promiscuous_guard);
    memset(&s_watch_session, 0, sizeof(s_watch_session));
    portEXIT_CRITICAL(&s_promiscuous_guard);
}

static void wifi_promiscuous_watch_task(void *parameter)
{
    wifi_promiscuous_watch_config_t config;
    wifi_promiscuous_result_writer_t write_line = NULL;
    void *context = NULL;
    uint32_t last_report_ms = 0U;
    esp_err_t err;

    (void)parameter;

    wifi_promiscuous_watch_copy_snapshot(NULL, NULL, NULL, &write_line, &context, &config);

    err = esp_wifi_set_channel(config.channel, WIFI_SECOND_CHAN_NONE);
    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_promiscuous_guard);
        wifi_promiscuous_reset_stats(&s_watch_session.stats, config.channel);
        s_watch_session.started_at_us = esp_timer_get_time();
        portEXIT_CRITICAL(&s_promiscuous_guard);

        err = wifi_promiscuous_begin_capture(
            WIFI_PROMISCUOUS_CAPTURE_WATCH,
            &s_watch_session.stats,
            config.rssi_min);
    }

    if (err != ESP_OK) {
        if (write_line != NULL) {
            write_line("ERR WIFI_WATCH_FAILED\n", context);
        }
        wifi_manager_clear_action();
        wifi_promiscuous_watch_finish();
        vTaskDelete(NULL);
        return;
    }

    if (write_line != NULL) {
        char line[WIFI_PROMISCUOUS_LINE_MAX_LENGTH];

        snprintf(
            line,
            sizeof(line),
            "WATCH_STARTED channel=%u interval_ms=%u\n",
            (unsigned int)config.channel,
            (unsigned int)config.report_interval_ms);
        write_line(line, context);
    }

    while (true) {
        bool stop_requested = false;
        wifi_promiscuous_stats_t snapshot;
        int64_t started_at_us = 0LL;
        uint32_t elapsed_ms;

        vTaskDelay(pdMS_TO_TICKS(WIFI_PROMISCUOUS_WATCH_TASK_POLL_MS));
        wifi_promiscuous_watch_copy_snapshot(
            &snapshot, &stop_requested, &started_at_us, NULL, NULL, NULL);
        elapsed_ms = (uint32_t)((esp_timer_get_time() - started_at_us) / 1000LL);

        if (stop_requested) {
            break;
        }

        if (elapsed_ms - last_report_ms >= config.report_interval_ms) {
            last_report_ms = elapsed_ms;
            wifi_promiscuous_write_watch_live_line(&snapshot, elapsed_ms, write_line, context);
        }
    }

    wifi_promiscuous_end_capture();

    {
        wifi_promiscuous_stats_t snapshot;
        int64_t started_at_us = 0LL;
        uint32_t duration_ms;
        char line[WIFI_PROMISCUOUS_LINE_MAX_LENGTH];

        wifi_promiscuous_watch_copy_snapshot(
            &snapshot, NULL, &started_at_us, NULL, NULL, NULL);
        duration_ms = (uint32_t)((esp_timer_get_time() - started_at_us) / 1000LL);

        wifi_promiscuous_write_watch_summary(&snapshot, duration_ms, write_line, context);
        snprintf(
            line,
            sizeof(line),
            "WATCH_DONE channel=%u duration_ms=%u\n",
            (unsigned int)snapshot.channel,
            (unsigned int)duration_ms);
        if (write_line != NULL) {
            write_line(line, context);
        }
    }

    wifi_promiscuous_watch_finish();
    wifi_manager_clear_action();
    vTaskDelete(NULL);
}

esp_err_t wifi_promiscuous_survey(
    const wifi_promiscuous_survey_config_t *config,
    wifi_promiscuous_result_writer_t write_line,
    void *context)
{
    wifi_promiscuous_stats_t stats;
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

    memset(&stats, 0, sizeof(stats));
    started_at_us = esp_timer_get_time();

    for (index = 0; index < config->channel_count; ++index) {
        wifi_promiscuous_reset_stats(&stats, config->channels[index]);

        err = wifi_promiscuous_begin_capture(
            WIFI_PROMISCUOUS_CAPTURE_SURVEY,
            &stats,
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

        if (stats.total_frames < recommended_total) {
            recommended_total = stats.total_frames;
            recommended_channel = stats.channel;
        }

        wifi_promiscuous_write_stats_line(
            "SURVEY",
            &stats,
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

esp_err_t wifi_promiscuous_watch_start(
    const wifi_promiscuous_watch_config_t *config,
    wifi_promiscuous_result_writer_t write_line,
    void *context)
{
    if (config == NULL || config->channel == 0U || config->channel > 14U ||
        config->report_interval_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_promiscuous_guard);
    if (s_capture_active || s_watch_session.active) {
        portEXIT_CRITICAL(&s_promiscuous_guard);
        return WIFI_PROMISCUOUS_ERR_BUSY;
    }

    memset(&s_watch_session, 0, sizeof(s_watch_session));
    s_watch_session.active = true;
    s_watch_session.config = *config;
    s_watch_session.write_line = write_line;
    s_watch_session.context = context;
    portEXIT_CRITICAL(&s_promiscuous_guard);

    if (xTaskCreate(
            wifi_promiscuous_watch_task,
            "wifi_watch",
            WIFI_PROMISCUOUS_WATCH_TASK_STACK_SIZE,
            NULL,
            tskIDLE_PRIORITY + 1U,
            &s_watch_session.task_handle) != pdPASS) {
        wifi_promiscuous_watch_finish();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t wifi_promiscuous_watch_stop(void)
{
    portENTER_CRITICAL(&s_promiscuous_guard);
    if (!s_watch_session.active) {
        portEXIT_CRITICAL(&s_promiscuous_guard);
        return ESP_OK;
    }

    s_watch_session.stop_requested = true;
    portEXIT_CRITICAL(&s_promiscuous_guard);

    for (uint32_t attempt = 0U; attempt < 60U; ++attempt) {
        if (!wifi_promiscuous_watch_is_active()) {
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(WIFI_PROMISCUOUS_WATCH_TASK_POLL_MS));
    }

    return ESP_ERR_TIMEOUT;
}

bool wifi_promiscuous_watch_is_active(void)
{
    bool active;

    portENTER_CRITICAL(&s_promiscuous_guard);
    active = s_watch_session.active;
    portEXIT_CRITICAL(&s_promiscuous_guard);

    return active;
}