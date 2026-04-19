#include "modules/wifi/beacon/wifi_beacon.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "modules/wifi/wifi_manager.h"

/* -----------------------------------------------------------------------
 * Safety note
 * Beacon frames are sent with esp_wifi_80211_tx() which requires the
 * radio to be in STA mode (IDLE).  The channel is fixed for the session
 * and TX power is capped at WIFI_BEACON_TX_POWER_MAX.  A background task
 * auto-stops the session once duration_ms elapses so the caller does not
 * have to issue an explicit BEACON_STOP.
 * ----------------------------------------------------------------------- */

#define WIFI_BEACON_TASK_STACK_SIZE     3072U
#define WIFI_BEACON_TASK_POLL_MS        10U

/* Minimum beacon header: MAC header (24) + fixed fields (12) */
#define BEACON_MAC_HEADER_LEN           24U
#define BEACON_FIXED_FIELDS_LEN         12U
#define BEACON_MIN_HEADER_LEN           (BEACON_MAC_HEADER_LEN + BEACON_FIXED_FIELDS_LEN)
/* Max frame: header + SSID IE (34) + DS IE (3) */
#define BEACON_MAX_FRAME_LEN            (BEACON_MIN_HEADER_LEN + 2U + WIFI_BEACON_MAX_SSID_LENGTH + 3U)

/* -----------------------------------------------------------------------
 * Internal frame cache
 * ----------------------------------------------------------------------- */

typedef struct {
    uint8_t frame[BEACON_MAX_FRAME_LEN];
    size_t frame_len;
} wifi_beacon_frame_t;

typedef struct {
    bool active;
    bool stop_requested;
    TaskHandle_t task_handle;
    wifi_beacon_config_t config;
    wifi_beacon_result_writer_t write_line;
    void *context;
    int64_t started_at_us;
    int8_t saved_tx_power;
    wifi_beacon_frame_t frames[WIFI_BEACON_MAX_SSIDS];
    size_t frame_count;
} wifi_beacon_session_t;

static portMUX_TYPE s_beacon_guard = portMUX_INITIALIZER_UNLOCKED;
static wifi_beacon_session_t s_session;

/* -----------------------------------------------------------------------
 * Frame builder
 *
 * Builds a minimal 802.11 beacon frame for the given SSID on the given
 * channel.  The BSSID/SA is a locally-administered unicast address derived
 * from a fixed base by incrementing the last byte with the SSID index.
 * ----------------------------------------------------------------------- */
static size_t wifi_beacon_build_frame(
    uint8_t *buf,
    size_t buf_size,
    const char *ssid,
    uint8_t channel,
    uint8_t ssid_index)
{
    size_t ssid_len;
    size_t pos = 0U;

    if (buf == NULL || ssid == NULL || buf_size < BEACON_MAX_FRAME_LEN) {
        return 0U;
    }

    ssid_len = strlen(ssid);
    if (ssid_len > WIFI_BEACON_MAX_SSID_LENGTH) {
        ssid_len = WIFI_BEACON_MAX_SSID_LENGTH;
    }

    memset(buf, 0, buf_size);

    /* --- Frame Control: beacon (type=0, subtype=8) --- */
    buf[pos++] = 0x80U; /* FC byte 0: subtype 8, type 0 */
    buf[pos++] = 0x00U; /* FC byte 1: flags */

    /* Duration */
    buf[pos++] = 0x00U;
    buf[pos++] = 0x00U;

    /* Destination: broadcast */
    buf[pos++] = 0xFFU; buf[pos++] = 0xFFU; buf[pos++] = 0xFFU;
    buf[pos++] = 0xFFU; buf[pos++] = 0xFFU; buf[pos++] = 0xFFU;

    /* Source address: locally-administered (bit 1 set, bit 0 clear) */
    buf[pos++] = 0x02U;
    buf[pos++] = 0x42U;
    buf[pos++] = 0xABU;
    buf[pos++] = 0xCD;
    buf[pos++] = 0xEFU;
    buf[pos++] = ssid_index; /* unique per SSID */

    /* BSSID: same as SA */
    buf[pos++] = 0x02U;
    buf[pos++] = 0x42U;
    buf[pos++] = 0xABU;
    buf[pos++] = 0xCDU;
    buf[pos++] = 0xEFU;
    buf[pos++] = ssid_index;

    /* Sequence control */
    buf[pos++] = 0x00U;
    buf[pos++] = 0x00U;

    /* --- Fixed fields (12 bytes) --- */

    /* Timestamp: 8 bytes, will be overwritten per-frame at TX time if HW supports,
       but zero is fine for injection */
    buf[pos++] = 0x00U; buf[pos++] = 0x00U; buf[pos++] = 0x00U; buf[pos++] = 0x00U;
    buf[pos++] = 0x00U; buf[pos++] = 0x00U; buf[pos++] = 0x00U; buf[pos++] = 0x00U;

    /* Beacon interval: 100 TU = 0x0064 little-endian */
    buf[pos++] = 0x64U;
    buf[pos++] = 0x00U;

    /* Capability info: ESS (bit 0) + short slot (bit 10) = 0x0421 little-endian */
    buf[pos++] = 0x21U;
    buf[pos++] = 0x04U;

    /* --- Tagged parameters --- */

    /* SSID IE */
    buf[pos++] = 0x00U;              /* Element ID: SSID */
    buf[pos++] = (uint8_t)ssid_len;
    memcpy(&buf[pos], ssid, ssid_len);
    pos += ssid_len;

    /* DS Parameter Set IE */
    buf[pos++] = 0x03U; /* Element ID: DS Parameter Set */
    buf[pos++] = 0x01U; /* Length */
    buf[pos++] = channel;

    return pos;
}

/* -----------------------------------------------------------------------
 * Session helpers
 * ----------------------------------------------------------------------- */

static void wifi_beacon_finish(void)
{
    portENTER_CRITICAL(&s_beacon_guard);
    s_session.active = false;
    s_session.stop_requested = false;
    s_session.task_handle = NULL;
    portEXIT_CRITICAL(&s_beacon_guard);
}

/* -----------------------------------------------------------------------
 * Background task
 * ----------------------------------------------------------------------- */

static void wifi_beacon_task(void *parameter)
{
    wifi_beacon_result_writer_t write_line = NULL;
    void *context = NULL;
    uint32_t duration_ms = 0U;
    char line[96];

    (void)parameter;

    portENTER_CRITICAL(&s_beacon_guard);
    write_line = s_session.write_line;
    context = s_session.context;
    duration_ms = s_session.config.duration_ms;
    s_session.started_at_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_beacon_guard);

    snprintf(
        line,
        sizeof(line),
        "BEACON_STARTED channel=%u ssids=%u duration_ms=%u\n",
        (unsigned int)s_session.config.channel,
        (unsigned int)s_session.frame_count,
        (unsigned int)duration_ms);
    if (write_line != NULL) {
        write_line(line, context);
    }

    while (true) {
        bool stop_requested = false;
        int64_t started_at_us = 0LL;
        uint32_t elapsed_ms = 0U;
        size_t frame_count = 0U;

        portENTER_CRITICAL(&s_beacon_guard);
        stop_requested = s_session.stop_requested;
        started_at_us = s_session.started_at_us;
        frame_count = s_session.frame_count;
        portEXIT_CRITICAL(&s_beacon_guard);

        elapsed_ms = (uint32_t)((esp_timer_get_time() - started_at_us) / 1000LL);

        if (stop_requested || elapsed_ms >= duration_ms) {
            break;
        }

        /* Send one beacon per SSID, then sleep for the interval */
        for (size_t index = 0U; index < frame_count; ++index) {
            /* Use en_sys_seq=true so the hardware fills the sequence counter */
            esp_wifi_80211_tx(
                WIFI_IF_STA,
                s_session.frames[index].frame,
                s_session.frames[index].frame_len,
                true);
        }

        vTaskDelay(pdMS_TO_TICKS(WIFI_BEACON_INTERVAL_MS));
    }

    /* Restore TX power */
    esp_wifi_set_max_tx_power(s_session.saved_tx_power);

    {
        uint32_t elapsed_ms =
            (uint32_t)((esp_timer_get_time() - s_session.started_at_us) / 1000LL);
        snprintf(
            line,
            sizeof(line),
            "BEACON_DONE channel=%u ssids=%u duration_ms=%u\n",
            (unsigned int)s_session.config.channel,
            (unsigned int)s_session.frame_count,
            (unsigned int)elapsed_ms);
    }
    if (write_line != NULL) {
        write_line(line, context);
    }

    wifi_manager_clear_action();
    wifi_beacon_finish();
    vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

esp_err_t wifi_beacon_start(
    const wifi_beacon_config_t *config,
    wifi_beacon_result_writer_t write_line,
    void *context)
{
    esp_err_t err;
    int8_t current_tx_power = 0;
    wifi_manager_status_t status;

    if (config == NULL || config->ssid_count == 0U ||
        config->ssid_count > WIFI_BEACON_MAX_SSIDS ||
        config->channel == 0U || config->channel > 14U ||
        config->duration_ms == 0U ||
        config->duration_ms > WIFI_BEACON_MAX_DURATION_MS) {
        return ESP_ERR_INVALID_ARG;
    }

    err = wifi_manager_refresh_status(&status);
    if (err != ESP_OK) {
        return err;
    }

    if (status.mode != WIFI_MANAGER_MODE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_beacon_guard);
    if (s_session.active) {
        portEXIT_CRITICAL(&s_beacon_guard);
        return WIFI_BEACON_ERR_BUSY;
    }
    portEXIT_CRITICAL(&s_beacon_guard);

    /* Set channel */
    err = esp_wifi_set_channel(config->channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        return err;
    }

    /* Cap TX power */
    esp_wifi_get_max_tx_power(&current_tx_power);
    esp_wifi_set_max_tx_power((int8_t)(WIFI_BEACON_TX_POWER_MAX * 4)); /* units: 0.25 dBm */

    /* Pre-build all frames */
    portENTER_CRITICAL(&s_beacon_guard);
    memset(&s_session, 0, sizeof(s_session));
    s_session.active = true;
    s_session.config = *config;
    s_session.write_line = write_line;
    s_session.context = context;
    s_session.saved_tx_power = current_tx_power;
    s_session.frame_count = 0U;
    portEXIT_CRITICAL(&s_beacon_guard);

    for (size_t index = 0U; index < config->ssid_count; ++index) {
        size_t len = wifi_beacon_build_frame(
            s_session.frames[index].frame,
            sizeof(s_session.frames[index].frame),
            config->ssids[index],
            config->channel,
            (uint8_t)index);
        if (len == 0U) {
            portENTER_CRITICAL(&s_beacon_guard);
            s_session.active = false;
            portEXIT_CRITICAL(&s_beacon_guard);
            esp_wifi_set_max_tx_power(current_tx_power);
            return ESP_ERR_INVALID_ARG;
        }
        s_session.frames[index].frame_len = len;
        s_session.frame_count = index + 1U;
    }

    err = wifi_manager_set_action(WIFI_MANAGER_ACTION_BEACONING);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_beacon_guard);
        s_session.active = false;
        portEXIT_CRITICAL(&s_beacon_guard);
        esp_wifi_set_max_tx_power(current_tx_power);
        return err;
    }

    BaseType_t task_result = xTaskCreate(
        wifi_beacon_task,
        "wifi_beacon",
        WIFI_BEACON_TASK_STACK_SIZE,
        NULL,
        5,
        &s_session.task_handle);

    if (task_result != pdPASS) {
        wifi_manager_clear_action();
        portENTER_CRITICAL(&s_beacon_guard);
        s_session.active = false;
        portEXIT_CRITICAL(&s_beacon_guard);
        esp_wifi_set_max_tx_power(current_tx_power);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t wifi_beacon_stop(void)
{
    portENTER_CRITICAL(&s_beacon_guard);
    if (!s_session.active) {
        portEXIT_CRITICAL(&s_beacon_guard);
        return WIFI_BEACON_ERR_NOT_ACTIVE;
    }
    s_session.stop_requested = true;
    portEXIT_CRITICAL(&s_beacon_guard);
    return ESP_OK;
}

bool wifi_beacon_is_active(void)
{
    bool active = false;
    portENTER_CRITICAL(&s_beacon_guard);
    active = s_session.active;
    portEXIT_CRITICAL(&s_beacon_guard);
    return active;
}
