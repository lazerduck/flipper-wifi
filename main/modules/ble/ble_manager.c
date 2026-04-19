#include "modules/ble/ble_manager.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define BLE_MANAGER_SCAN_DURATION_MS 3000
#define BLE_MANAGER_SCAN_WAIT_MS     4500
#define BLE_MANAGER_LINE_SIZE        160
#define BLE_MANAGER_NAME_SIZE        33

typedef struct {
    ble_addr_t address;
    char name[BLE_MANAGER_NAME_SIZE];
    int8_t rssi;
    bool has_name;
    bool connectable;
    bool in_use;
} ble_scan_result_t;

typedef struct {
    ble_scan_result_t results[BLE_MANAGER_MAX_RESULTS];
    SemaphoreHandle_t done_semaphore;
    uint16_t total_count;
    int completion_reason;
} ble_scan_session_t;

static bool s_ble_initialized = false;
static bool s_ble_synced = false;
static SemaphoreHandle_t s_ble_sync_semaphore = NULL;
static SemaphoreHandle_t s_ble_scan_mutex = NULL;

static const char *ble_manager_addr_type_to_string(uint8_t type)
{
    switch (type) {
    case BLE_ADDR_PUBLIC:
        return "PUBLIC";
    case BLE_ADDR_RANDOM:
        return "RANDOM";
    case BLE_ADDR_PUBLIC_ID:
        return "PUBLIC_ID";
    case BLE_ADDR_RANDOM_ID:
        return "RANDOM_ID";
    default:
        return "UNKNOWN";
    }
}

static bool ble_manager_event_is_connectable(uint8_t event_type)
{
    return event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
           event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
}

static void ble_manager_format_mac(const ble_addr_t *address, char *buffer, size_t buffer_size)
{
    if (address == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    snprintf(
        buffer,
        buffer_size,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        address->val[5],
        address->val[4],
        address->val[3],
        address->val[2],
        address->val[1],
        address->val[0]);
}

static int ble_manager_find_result(const ble_scan_session_t *session, const ble_addr_t *address)
{
    if (session == NULL || address == NULL) {
        return -1;
    }

    for (size_t index = 0; index < BLE_MANAGER_MAX_RESULTS; ++index) {
        if (!session->results[index].in_use) {
            continue;
        }

        if (session->results[index].address.type == address->type &&
            memcmp(session->results[index].address.val, address->val, sizeof(address->val)) == 0) {
            return (int)index;
        }
    }

    return -1;
}

static int ble_manager_reserve_result(ble_scan_session_t *session, const ble_addr_t *address)
{
    if (session == NULL || address == NULL) {
        return -1;
    }

    for (size_t index = 0; index < BLE_MANAGER_MAX_RESULTS; ++index) {
        if (session->results[index].in_use) {
            continue;
        }

        session->results[index].address = *address;
        session->results[index].in_use = true;
        session->results[index].rssi = -127;
        ++session->total_count;
        return (int)index;
    }

    ++session->total_count;
    return -1;
}

static void ble_manager_on_reset(int reason)
{
    (void)reason;
    s_ble_synced = false;
}

static void ble_manager_on_sync(void)
{
    s_ble_synced = true;

    if (s_ble_sync_semaphore != NULL) {
        xSemaphoreGive(s_ble_sync_semaphore);
    }
}

static void ble_manager_host_task(void *parameter)
{
    (void)parameter;

    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t ble_manager_init(void)
{
    esp_err_t err;

    if (s_ble_initialized) {
        return ESP_OK;
    }

    if (s_ble_sync_semaphore == NULL) {
        s_ble_sync_semaphore = xSemaphoreCreateBinary();
    }
    if (s_ble_scan_mutex == NULL) {
        s_ble_scan_mutex = xSemaphoreCreateMutex();
    }
    if (s_ble_sync_semaphore == NULL || s_ble_scan_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }

    ble_hs_cfg.reset_cb = ble_manager_on_reset;
    ble_hs_cfg.sync_cb = ble_manager_on_sync;

    while (xSemaphoreTake(s_ble_sync_semaphore, 0) == pdTRUE) {
    }

    nimble_port_freertos_init(ble_manager_host_task);

    if (!s_ble_synced &&
        xSemaphoreTake(s_ble_sync_semaphore, pdMS_TO_TICKS(BLE_MANAGER_SCAN_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_ble_initialized = true;
    return ESP_OK;
}

static int ble_manager_gap_event(struct ble_gap_event *event, void *arg)
{
    ble_scan_session_t *session = (ble_scan_session_t *)arg;

    if (session == NULL) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
    {
        struct ble_hs_adv_fields fields;
        int result_index = ble_manager_find_result(session, &event->disc.addr);

        if (result_index < 0) {
            result_index = ble_manager_reserve_result(session, &event->disc.addr);
        }

        if (result_index < 0) {
            return 0;
        }

        ble_scan_result_t *result = &session->results[result_index];
        result->connectable = ble_manager_event_is_connectable(event->disc.event_type);
        if (event->disc.rssi > result->rssi) {
            result->rssi = event->disc.rssi;
        }

        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0 &&
            fields.name != NULL && fields.name_len > 0) {
            size_t copy_length = fields.name_len;
            if (copy_length >= sizeof(result->name)) {
                copy_length = sizeof(result->name) - 1U;
            }

            memcpy(result->name, fields.name, copy_length);
            result->name[copy_length] = '\0';
            result->has_name = true;
        }

        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        session->completion_reason = event->disc_complete.reason;
        xSemaphoreGive(session->done_semaphore);
        return 0;
    default:
        return 0;
    }
}

esp_err_t ble_manager_scan(ble_scan_result_writer_t write_line, void *context)
{
    ble_scan_session_t session = {0};
    struct ble_gap_disc_params scan_params = {0};
    uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
    esp_err_t err;
    int rc;

    if (write_line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = ble_manager_init();
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(s_ble_scan_mutex, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    session.done_semaphore = xSemaphoreCreateBinary();
    if (session.done_semaphore == NULL) {
        xSemaphoreGive(s_ble_scan_mutex);
        return ESP_ERR_NO_MEM;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        vSemaphoreDelete(session.done_semaphore);
        xSemaphoreGive(s_ble_scan_mutex);
        return ESP_FAIL;
    }

    scan_params.passive = 1;
    scan_params.filter_duplicates = 1;

    rc = ble_gap_disc(
        own_addr_type,
        BLE_MANAGER_SCAN_DURATION_MS,
        &scan_params,
        ble_manager_gap_event,
        &session);
    if (rc != 0) {
        vSemaphoreDelete(session.done_semaphore);
        xSemaphoreGive(s_ble_scan_mutex);
        return ESP_FAIL;
    }

    if (xSemaphoreTake(session.done_semaphore, pdMS_TO_TICKS(BLE_MANAGER_SCAN_WAIT_MS)) != pdTRUE) {
        ble_gap_disc_cancel();
        vSemaphoreDelete(session.done_semaphore);
        xSemaphoreGive(s_ble_scan_mutex);
        return ESP_ERR_TIMEOUT;
    }

    if (session.completion_reason != 0) {
        vSemaphoreDelete(session.done_semaphore);
        xSemaphoreGive(s_ble_scan_mutex);
        return ESP_FAIL;
    }

    char line[BLE_MANAGER_LINE_SIZE];
    snprintf(line, sizeof(line), "BLE_SCAN_COUNT %u\n", (unsigned)session.total_count);
    write_line(line, context);

    for (size_t index = 0; index < BLE_MANAGER_MAX_RESULTS; ++index) {
        if (!session.results[index].in_use) {
            continue;
        }

        char mac[18];
        ble_manager_format_mac(&session.results[index].address, mac, sizeof(mac));
        snprintf(
            line,
            sizeof(line),
            "BLE_DEVICE %s RSSI %d ADDR %s CONN %s NAME %s\n",
            mac,
            session.results[index].rssi,
            ble_manager_addr_type_to_string(session.results[index].address.type),
            session.results[index].connectable ? "YES" : "NO",
            session.results[index].has_name ? session.results[index].name : "-");
        write_line(line, context);
    }

    if (session.total_count > BLE_MANAGER_MAX_RESULTS) {
        snprintf(
            line,
            sizeof(line),
            "BLE_SCAN_TRUNCATED %u\n",
            (unsigned)(session.total_count - BLE_MANAGER_MAX_RESULTS));
        write_line(line, context);
    }

    write_line("BLE_SCAN_DONE\n", context);

    vSemaphoreDelete(session.done_semaphore);
    xSemaphoreGive(s_ble_scan_mutex);
    return ESP_OK;
}