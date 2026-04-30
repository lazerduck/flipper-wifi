#include "modules/ble/ble_command.h"

#include "esp_err.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host/ble_gap.h"
#include "modules/ble/ble_manager.h"
#include "modules/ble/ble_gatt.h"
#include "modules/status_led/status_led.h"

#define BLE_COMMAND_PREFIX "BLE"
#define BLE_SCAN_COMMAND "BLE SCAN"
#define BLE_SCAN_PREFIX "BLE SCAN "
#define BLE_STATUS_COMMAND "BLE STATUS"
#define BLE_GATT_PREFIX "BLE GATT"
#define BLE_GATT_FULL_PREFIX "BLE GATT "
#define BLE_GATT_MAC_KEY "mac="
#define BLE_GATT_ADDR_TYPE_KEY "addr_type="
#define BLE_DISTANCE_PREFIX "BLE DISTANCE"
#define BLE_DISTANCE_START_PREFIX "BLE DISTANCE START"
#define BLE_DISTANCE_STOP_COMMAND "BLE DISTANCE STOP"
#define BLE_DISTANCE_MAC_KEY "mac="
#define BLE_DISTANCE_INTERVAL_KEY "interval_ms="
#define BLE_DISTANCE_TX_POWER_KEY "tx_power="
#define BLE_DISTANCE_DEFAULT_TX_POWER_DBM (-59)
#define BLE_DISTANCE_TASK_STACK_SIZE 4096U
#define BLE_DISTANCE_TASK_STOP_WAIT_MS 100U
#define BLE_DISTANCE_TASK_STOP_MAX_POLLS 30U
/* Minimum gap between consecutive RSSI samples sent to the Flipper (ms).
 * The interval_ms argument from the Flipper is clamped to this floor. */
#define BLE_DISTANCE_MIN_SAMPLE_MS 250U
/* Emit a "not seen" sample after the target has been absent this long (ms). */
#define BLE_DISTANCE_NOT_SEEN_REPORT_MS 3000U

typedef struct {
    bool active;
    volatile bool stop_requested;
    TaskHandle_t task_handle;
    char mac[18];
    uint16_t interval_ms;
    int8_t tx_power_dbm;
    int16_t last_rssi;
    uint32_t sample_count;
    command_response_writer_t write_response;
} ble_distance_session_t;

static ble_distance_session_t s_ble_distance_session;

typedef struct {
    const command_context_t *command_context;
} ble_writer_context_t;

static void ble_write_result_line(const char *line, void *context)
{
    const ble_writer_context_t *writer_context = (const ble_writer_context_t *)context;

    if (writer_context == NULL) {
        return;
    }

    command_context_write_line(writer_context->command_context, line);
}

static bool ble_read_named_arg(
    const char *args,
    const char *key,
    char *value,
    size_t value_size)
{
    const char *start;
    size_t length = 0U;

    if (args == NULL || key == NULL || value == NULL || value_size == 0U) {
        return false;
    }

    start = strstr(args, key);
    if (start == NULL) {
        return false;
    }

    start += strlen(key);
    if (start[0] == '\0') {
        return false;
    }

    while (start[length] != '\0' && start[length] != ' ') {
        length++;
    }

    if (length == 0U || length >= value_size) {
        return false;
    }

    memcpy(value, start, length);
    value[length] = '\0';
    return true;
}

static bool ble_normalize_mac(const char *input, char *output, size_t output_size)
{
    if (input == NULL || output == NULL || output_size < 18U || strlen(input) != 17U) {
        return false;
    }

    for (size_t index = 0U; index < 17U; index++) {
        const char ch = input[index];
        if ((index % 3U) == 2U) {
            if (ch != ':' && ch != '-') {
                return false;
            }
            output[index] = ':';
        } else {
            if (!isxdigit((unsigned char)ch)) {
                return false;
            }
            output[index] = (char)toupper((unsigned char)ch);
        }
    }

    output[17] = '\0';
    return true;
}

static void ble_distance_write_line(const ble_distance_session_t *session, const char *line)
{
    if (session == NULL || session->write_response == NULL || line == NULL) {
        return;
    }

    session->write_response(line);
}

static int16_t ble_distance_estimate_distance_dm(int16_t rssi, int8_t tx_power_dbm)
{
    const float n = 2.2f;
    const float exponent = ((float)tx_power_dbm - (float)rssi) / (10.0f * n);
    float meters = powf(10.0f, exponent);

    if (meters < 0.1f) {
        meters = 0.1f;
    }
    if (meters > 99.9f) {
        meters = 99.9f;
    }

    return (int16_t)lroundf(meters * 10.0f);
}

static const char *ble_distance_trend_name(int16_t previous_rssi, int16_t current_rssi)
{
    const int16_t delta = current_rssi - previous_rssi;

    if (delta >= 3) {
        return "CLOSER";
    }
    if (delta <= -3) {
        return "FARTHER";
    }
    return "STEADY";
}

typedef struct {
    ble_distance_session_t *session;
    uint32_t last_sample_ms;  /* millis of last emitted sample (0 = never) */
    uint32_t last_any_ms;     /* millis of last advertisement from any device */
} ble_distance_stream_ctx_t;

/* Called from the NimBLE host task for every received advertisement.
 * Filters on the target MAC, rate-limits samples, and emits a
 * BLE_DISTANCE_SAMPLE line directly over UART (which is mutex-protected).
 * Also emits a "not seen" sample when the target has been absent too long. */
static void ble_distance_stream_on_adv(
    const ble_addr_t *addr,
    int8_t rssi,
    const uint8_t *adv_data,
    uint8_t adv_data_len,
    void *context)
{
    ble_distance_stream_ctx_t *ctx = (ble_distance_stream_ctx_t *)context;
    char mac[18];
    char line[128];
    uint32_t now_ms;

    (void)adv_data;
    (void)adv_data_len;

    if (ctx == NULL || ctx->session == NULL || addr == NULL) {
        return;
    }

    now_ms = (uint32_t)(xTaskGetTickCount()) * portTICK_PERIOD_MS;

    /* Track that we are receiving any BLE traffic (used for not-seen timeout) */
    ctx->last_any_ms = now_ms;

    /* Format MAC from address bytes (big-endian display, little-endian storage) */
    snprintf(
        mac,
        sizeof(mac),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        addr->val[5], addr->val[4], addr->val[3],
        addr->val[2], addr->val[1], addr->val[0]);

    if (strcmp(mac, ctx->session->mac) != 0) {
        /* Not our target — but check whether the target has gone silent */
        if (ctx->last_sample_ms != 0U &&
            (now_ms - ctx->last_sample_ms) >= BLE_DISTANCE_NOT_SEEN_REPORT_MS) {
            ctx->last_sample_ms = now_ms;
            snprintf(
                line,
                sizeof(line),
                "BLE_DISTANCE_SAMPLE mac=%s rssi=-127 seen=0 trend=LOST dist_dm=-1 samples=%u\n",
                ctx->session->mac,
                (unsigned)ctx->session->sample_count);
            ble_distance_write_line(ctx->session, line);
        }
        return;
    }

    /* Our target is visible — apply rate limit */
    const uint16_t min_interval_ms =
        (ctx->session->interval_ms > BLE_DISTANCE_MIN_SAMPLE_MS)
            ? ctx->session->interval_ms
            : BLE_DISTANCE_MIN_SAMPLE_MS;

    if (ctx->last_sample_ms != 0U &&
        (now_ms - ctx->last_sample_ms) < (uint32_t)min_interval_ms) {
        return;
    }

    /* Emit a sample */
    const int16_t distance_dm =
        ble_distance_estimate_distance_dm((int16_t)rssi, ctx->session->tx_power_dbm);
    const char *trend =
        (ctx->session->sample_count == 0U)
            ? "STEADY"
            : ble_distance_trend_name(ctx->session->last_rssi, (int16_t)rssi);

    ctx->session->sample_count++;
    ctx->session->last_rssi = (int16_t)rssi;
    ctx->last_sample_ms = now_ms;

    snprintf(
        line,
        sizeof(line),
        "BLE_DISTANCE_SAMPLE mac=%s rssi=%d seen=1 trend=%s dist_dm=%d samples=%u\n",
        ctx->session->mac,
        (int)rssi,
        trend,
        (int)distance_dm,
        (unsigned)ctx->session->sample_count);
    ble_distance_write_line(ctx->session, line);
}

static void ble_distance_task(void *parameter)
{
    ble_distance_session_t *session = (ble_distance_session_t *)parameter;
    char line[128];

    if (session == NULL) {
        vTaskDelete(NULL);
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "BLE_DISTANCE_STARTED mac=%s interval_ms=%u\n",
        session->mac,
        (unsigned)session->interval_ms);
    ble_distance_write_line(session, line);

    (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_SCANNING);

    ble_distance_stream_ctx_t stream_ctx = {
        .session = session,
        .last_sample_ms = 0U,
        .last_any_ms = 0U,
    };

    const esp_err_t err = ble_manager_scan_stream(
        ble_distance_stream_on_adv,
        &stream_ctx,
        &session->stop_requested);

    if (err == ESP_ERR_INVALID_STATE) {
        ble_distance_write_line(session, "ERR BLE_DISTANCE_BUSY\n");
    } else if (err == ESP_ERR_NO_MEM) {
        ble_distance_write_line(session, "ERR BLE_DISTANCE_NO_MEM\n");
    } else if (err != ESP_OK) {
        ble_distance_write_line(session, "ERR BLE_DISTANCE_FAILED\n");
    }

    (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_IDLE);

    snprintf(
        line,
        sizeof(line),
        "BLE_DISTANCE_DONE mac=%s samples=%u\n",
        session->mac,
        (unsigned)session->sample_count);
    ble_distance_write_line(session, line);

    memset(session, 0, sizeof(*session));
    vTaskDelete(NULL);
}

static esp_err_t ble_distance_start(
    const char *mac,
    uint16_t interval_ms,
    int8_t tx_power_dbm,
    command_response_writer_t write_response)
{
    if (mac == NULL || write_response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ble_distance_session.active) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ble_distance_session, 0, sizeof(s_ble_distance_session));
    s_ble_distance_session.active = true;
    s_ble_distance_session.interval_ms = interval_ms;
    s_ble_distance_session.tx_power_dbm = tx_power_dbm;
    s_ble_distance_session.last_rssi = -127;
    s_ble_distance_session.write_response = write_response;
    snprintf(s_ble_distance_session.mac, sizeof(s_ble_distance_session.mac), "%s", mac);

    if (xTaskCreate(
            ble_distance_task,
            "ble_distance",
            BLE_DISTANCE_TASK_STACK_SIZE,
            &s_ble_distance_session,
            tskIDLE_PRIORITY + 1U,
            &s_ble_distance_session.task_handle) != pdPASS) {
        memset(&s_ble_distance_session, 0, sizeof(s_ble_distance_session));
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t ble_distance_stop(void)
{
    if (!s_ble_distance_session.active) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ble_distance_session.stop_requested = true;

    /* Cancel the continuous scan immediately rather than waiting for the
     * next advertisement to arrive — the DISC_COMPLETE event will fire
     * and unblock ble_manager_scan_stream() in the distance task. */
    ble_gap_disc_cancel();

    for (uint8_t index = 0U; index < BLE_DISTANCE_TASK_STOP_MAX_POLLS; index++) {
        if (!s_ble_distance_session.active) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(BLE_DISTANCE_TASK_STOP_WAIT_MS));
    }

    return ESP_ERR_TIMEOUT;
}

bool ble_command_try_handle(const char *command_line, const command_context_t *context)
{
    if (command_line == NULL || context == NULL) {
        return false;
    }

    if (strcmp(command_line, BLE_COMMAND_PREFIX) == 0) {
        command_context_write_line(context, "ERR USAGE BLE <SCAN|STATUS|GATT|DISTANCE>\n");
        return true;
    }

    if (strcmp(command_line, BLE_SCAN_COMMAND) == 0 ||
        strncmp(command_line, BLE_SCAN_PREFIX, strlen(BLE_SCAN_PREFIX)) == 0) {
        ble_writer_context_t writer_context = {
            .command_context = context,
        };
        char line[32];
        uint16_t scan_duration_ms = ble_manager_default_scan_duration_ms();

        if (strncmp(command_line, BLE_SCAN_PREFIX, strlen(BLE_SCAN_PREFIX)) == 0) {
            const char *value = command_line + strlen(BLE_SCAN_PREFIX);
            char *end = NULL;
            unsigned long parsed;

            if (value[0] == '\0') {
                command_context_write_line(context, "ERR USAGE BLE SCAN [duration_ms]\n");
                return true;
            }

            parsed = strtoul(value, &end, 10);
            if (end == value || *end != '\0') {
                command_context_write_line(context, "ERR USAGE BLE SCAN [duration_ms]\n");
                return true;
            }

            scan_duration_ms = ble_manager_clamp_scan_duration_ms(parsed);
        }

        snprintf(
            line,
            sizeof(line),
            "BLE_SCAN_START %u\n",
            (unsigned)scan_duration_ms);
        command_context_write_line(context, line);
        (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_SCANNING);
        esp_err_t err = ble_manager_scan(scan_duration_ms, ble_write_result_line, &writer_context);
        (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_IDLE);

        if (err == ESP_ERR_INVALID_STATE) {
            command_context_write_line(context, "ERR BLE_SCAN_BUSY\n");
        } else if (err == ESP_ERR_TIMEOUT) {
            command_context_write_line(context, "ERR BLE_SCAN_TIMEOUT\n");
        } else if (err == ESP_ERR_NO_MEM) {
            command_context_write_line(context, "ERR BLE_SCAN_NO_MEM\n");
        } else if (err != ESP_OK) {
            command_context_write_line(context, "ERR BLE_SCAN_FAILED\n");
        }
        return true;
    }

    if (strcmp(command_line, BLE_STATUS_COMMAND) == 0) {
        command_context_write_line(context, "ERR BLE_STATUS_NOT_IMPLEMENTED\n");
        return true;
    }

    if (strcmp(command_line, BLE_DISTANCE_PREFIX) == 0) {
        command_context_write_line(
            context,
            "ERR USAGE BLE DISTANCE <START mac=<MAC> [interval_ms=<ms>] [tx_power=<dbm>]|STOP>\n");
        return true;
    }

    if (strcmp(command_line, BLE_DISTANCE_STOP_COMMAND) == 0) {
        const esp_err_t err = ble_distance_stop();
        if (err == ESP_ERR_INVALID_STATE) {
            command_context_write_line(context, "ERR BLE_DISTANCE_NOT_ACTIVE\n");
        } else if (err == ESP_ERR_TIMEOUT) {
            command_context_write_line(context, "ERR BLE_DISTANCE_STOP_TIMEOUT\n");
        }
        return true;
    }

    if (strncmp(command_line, BLE_DISTANCE_START_PREFIX " ", strlen(BLE_DISTANCE_START_PREFIX " ")) == 0) {
        const char *args = command_line + strlen(BLE_DISTANCE_START_PREFIX) + 1U;
        char mac_value[24] = {0};
        char normalized_mac[18] = {0};
        char interval_value[16] = {0};
        char tx_power_value[16] = {0};
        uint16_t interval_ms = ble_manager_default_scan_duration_ms();
        int8_t tx_power_dbm = BLE_DISTANCE_DEFAULT_TX_POWER_DBM;

        if (!ble_read_named_arg(args, BLE_DISTANCE_MAC_KEY, mac_value, sizeof(mac_value)) ||
            !ble_normalize_mac(mac_value, normalized_mac, sizeof(normalized_mac))) {
            command_context_write_line(context, "ERR BLE_DISTANCE_INVALID_ARGS\n");
            return true;
        }

        if (ble_read_named_arg(args, BLE_DISTANCE_INTERVAL_KEY, interval_value, sizeof(interval_value))) {
            char *end = NULL;
            const unsigned long parsed = strtoul(interval_value, &end, 10);
            if (end == interval_value || *end != '\0') {
                command_context_write_line(context, "ERR BLE_DISTANCE_INVALID_ARGS\n");
                return true;
            }
            interval_ms = ble_manager_clamp_scan_duration_ms(parsed);
        }

        if (ble_read_named_arg(args, BLE_DISTANCE_TX_POWER_KEY, tx_power_value, sizeof(tx_power_value))) {
            char *end = NULL;
            const long parsed = strtol(tx_power_value, &end, 10);
            if (end == tx_power_value || *end != '\0' || parsed < -127L || parsed > 20L) {
                command_context_write_line(context, "ERR BLE_DISTANCE_INVALID_ARGS\n");
                return true;
            }
            tx_power_dbm = (int8_t)parsed;
        }

        const esp_err_t err =
            ble_distance_start(normalized_mac, interval_ms, tx_power_dbm, context->write_response);
        if (err == ESP_ERR_INVALID_STATE) {
            command_context_write_line(context, "ERR BLE_DISTANCE_BUSY\n");
        } else if (err == ESP_ERR_NO_MEM) {
            command_context_write_line(context, "ERR BLE_DISTANCE_NO_MEM\n");
        } else if (err != ESP_OK) {
            command_context_write_line(context, "ERR BLE_DISTANCE_FAILED\n");
        }
        return true;
    }

    if (strcmp(command_line, BLE_GATT_PREFIX) == 0 ||
        strncmp(command_line, BLE_GATT_FULL_PREFIX, strlen(BLE_GATT_FULL_PREFIX)) == 0) {
        const char *args = NULL;
        char mac_buf[18] = {0};
        char addr_type_buf[16] = {0};
        const char *mac_ptr = NULL;
        const char *addr_type_ptr = NULL;

        if (strcmp(command_line, BLE_GATT_PREFIX) == 0) {
            command_context_write_line(context, "ERR USAGE BLE GATT mac=<MAC> [addr_type=<PUBLIC|RANDOM|PUBLIC_ID|RANDOM_ID>]\n");
            return true;
        }

        args = command_line + strlen(BLE_GATT_FULL_PREFIX);

        /* Parse mac= token */
        mac_ptr = strstr(args, BLE_GATT_MAC_KEY);
        if (mac_ptr != NULL) {
            mac_ptr += strlen(BLE_GATT_MAC_KEY);
            size_t i = 0;
            while (i < sizeof(mac_buf) - 1U && mac_ptr[i] != '\0' && mac_ptr[i] != ' ') {
                mac_buf[i] = mac_ptr[i];
                i++;
            }
            mac_buf[i] = '\0';
        }

        if (mac_buf[0] == '\0') {
            command_context_write_line(context, "ERR BLE_GATT_INVALID_ARGS\n");
            return true;
        }

        /* Parse optional addr_type= token */
        addr_type_ptr = strstr(args, BLE_GATT_ADDR_TYPE_KEY);
        if (addr_type_ptr != NULL) {
            addr_type_ptr += strlen(BLE_GATT_ADDR_TYPE_KEY);
            size_t i = 0;
            while (i < sizeof(addr_type_buf) - 1U &&
                   addr_type_ptr[i] != '\0' &&
                   addr_type_ptr[i] != ' ') {
                addr_type_buf[i] = addr_type_ptr[i];
                i++;
            }
            addr_type_buf[i] = '\0';
        }

        ble_writer_context_t writer_context = {
            .command_context = context,
        };

        (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_CONNECTING);

        esp_err_t err = ble_gatt_inspect(
            mac_buf,
            addr_type_buf[0] != '\0' ? addr_type_buf : NULL,
            ble_write_result_line,
            &writer_context);
        (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_IDLE);

        if (err == ESP_ERR_INVALID_STATE) {
            command_context_write_line(context, "ERR BLE_GATT_BUSY\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            command_context_write_line(context, "ERR BLE_GATT_INVALID_ARGS\n");
        } else if (err == ESP_ERR_TIMEOUT) {
            /* timeout message already emitted by ble_gatt_inspect */
        } else if (err != ESP_OK) {
            /* error message already emitted by ble_gatt_inspect */
        }
        return true;
    }

    if (strncmp(command_line, BLE_COMMAND_PREFIX " ", strlen(BLE_COMMAND_PREFIX " ")) == 0) {
        command_context_write_line(context, "ERR UNKNOWN_BLE_COMMAND\n");
        return true;
    }

    return false;
}