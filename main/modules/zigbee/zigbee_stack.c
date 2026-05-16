#include "modules/zigbee/zigbee_stack.h"

#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee.h"
#include "ezbee/af.h"
#include "ezbee/zcl/cluster/on_off.h"
#include "ezbee/zha.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define ZIGBEE_STACK_TAG "zigbee_stack"
#define ZIGBEE_STACK_TASK_NAME "zigbee_main"
#define ZIGBEE_STACK_TASK_STACK 6144U
#define ZIGBEE_STACK_TASK_PRIO 5U
#define ZIGBEE_STACK_DEFAULT_JOIN_TIMEOUT_MS 120000U
#define ZIGBEE_STORAGE_PARTITION_NAME "zb_storage"
#define ZIGBEE_SWITCH_ENDPOINT 1U
#define ZIGBEE_STEERING_ATTEMPT_MAX_WAIT_MS 25000U
#define ZIGBEE_STEERING_MAX_NO_NETWORK_RETRIES 10U
#define ZIGBEE_PRE_JOIN_RESET_TIMEOUT_MS 5000U
/* ZIGBEE_BDB_STATUS_NO_NETWORK is defined in zigbee_stack.h */

#define ZIGBEE_CHANNEL_TO_MASK(ch) (1U << (ch))

#define ZIGBEE_ZED_CONFIG()                              \
    {                                                    \
        .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,  \
        .install_code_policy = false,                   \
        .zed_config = {                                 \
            .ed_timeout = EZB_NWK_ED_TIMEOUT_64MIN,     \
            .keep_alive = 4000,                         \
        },                                               \
    }

#if CONFIG_SOC_IEEE802154_SUPPORTED
#define ZIGBEE_PLATFORM_CONFIG()                                  \
    {                                                             \
        .storage_partition_name = ZIGBEE_STORAGE_PARTITION_NAME,  \
        .radio_config = {                                         \
            .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,           \
        },                                                        \
    }
#else
#error "This target does not support native IEEE 802.15.4 radio mode."
#endif

typedef struct {
    bool initialized;
    bool stack_started;
    bool join_in_progress;
    bool leave_in_progress;
    bool join_ok;
    bool leave_ok;
    zigbee_stack_network_info_t net;
    TaskHandle_t stack_task;
    SemaphoreHandle_t state_mutex;
    SemaphoreHandle_t stack_ready_sem;
    SemaphoreHandle_t join_sem;
    SemaphoreHandle_t leave_sem;
    bool endpoint_registered;
    uint8_t last_steering_status;
} zigbee_stack_state_t;

static zigbee_stack_state_t s_stack;

static void zigbee_stack_copy_network_locked(zigbee_stack_network_info_t* out_info) {
    if(out_info == NULL) {
        return;
    }

    memcpy(out_info, &s_stack.net, sizeof(*out_info));
}

static void zigbee_stack_refresh_network_locked(void) {
    if(ezb_bdb_dev_joined()) {
        ezb_extpanid_t ext_pan = {0};
        s_stack.net.joined = true;
        s_stack.net.pan_id = ezb_nwk_get_panid();
        s_stack.net.channel = ezb_nwk_get_current_channel();
        s_stack.net.short_addr = ezb_nwk_get_short_address();
        ezb_nwk_get_extended_panid(&ext_pan);
        s_stack.net.ext_pan_id = ext_pan.u64;
    } else {
        s_stack.net.joined = false;
        s_stack.net.pan_id = 0U;
        s_stack.net.channel = 0U;
        s_stack.net.short_addr = 0U;
        s_stack.net.ext_pan_id = 0ULL;
    }
}

static bool zigbee_stack_app_signal_handler(const ezb_app_signal_t* app_signal) {
    ezb_app_signal_type_t signal_type;

    if(app_signal == NULL) {
        return true;
    }

    signal_type = ezb_app_signal_get_type(app_signal);

    switch(signal_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(ZIGBEE_STACK_TAG, "Initializing Zigbee stack");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t*)ezb_app_signal_get_params(app_signal));
        xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
        s_stack.stack_started = (status == EZB_BDB_STATUS_SUCCESS);
        if(s_stack.stack_started) {
            zigbee_stack_refresh_network_locked();
        }
        xSemaphoreGive(s_stack.state_mutex);

        if(status == EZB_BDB_STATUS_SUCCESS) {
            xSemaphoreGive(s_stack.stack_ready_sem);
        } else {
            ESP_LOGW(
                ZIGBEE_STACK_TAG,
                "Zigbee stack startup failed status=0x%02x",
                (unsigned)status);
        }
        break;
    }

    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t*)ezb_app_signal_get_params(app_signal));
        xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
        s_stack.join_ok = (status == EZB_BDB_STATUS_SUCCESS);
        s_stack.last_steering_status = (uint8_t)status;
        s_stack.join_in_progress = false;
        zigbee_stack_refresh_network_locked();
        xSemaphoreGive(s_stack.state_mutex);

        if(status == EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGI(
                ZIGBEE_STACK_TAG,
                "Joined network pan=0x%04x ch=%u short=0x%04x",
                s_stack.net.pan_id,
                (unsigned)s_stack.net.channel,
                s_stack.net.short_addr);
        } else {
            ESP_LOGW(
                ZIGBEE_STACK_TAG,
                "Network steering failed status=0x%02x",
                (unsigned)status);
        }

        xSemaphoreGive(s_stack.join_sem);
        break;
    }

    case EZB_ZDO_SIGNAL_LEAVE: {
        const ezb_zdo_signal_leave_params_t* leave_params = ezb_app_signal_get_params(app_signal);
        xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
        s_stack.leave_ok = true;
        s_stack.leave_in_progress = false;
        zigbee_stack_refresh_network_locked();
        xSemaphoreGive(s_stack.state_mutex);

        ESP_LOGI(
            ZIGBEE_STACK_TAG,
            "Left network type=0x%02x",
            leave_params != NULL ? leave_params->leave_type : 0U);

        xSemaphoreGive(s_stack.leave_sem);
        break;
    }

    default:
        break;
    }

    return true;
}

static void zigbee_stack_task(void* pvParameter) {
    esp_zigbee_config_t config = {
        .device_config = ZIGBEE_ZED_CONFIG(),
        .platform_config = ZIGBEE_PLATFORM_CONFIG(),
    };

    ESP_UNUSED(pvParameter);

    ESP_LOGI(ZIGBEE_STACK_TAG, "Starting Zigbee task");

    ESP_ERROR_CHECK(esp_zigbee_init(&config));

    ezb_zha_on_off_switch_config_t switch_cfg = EZB_ZHA_ON_OFF_SWITCH_CONFIG();
    ezb_af_ep_desc_t switch_ep = ezb_zha_create_on_off_switch(ZIGBEE_SWITCH_ENDPOINT, &switch_cfg);
    if(switch_ep == EZB_INVALID_AF_EP_DESC) {
        ESP_LOGE(ZIGBEE_STACK_TAG, "Failed to create on/off switch endpoint");
        vTaskDelete(NULL);
        return;
    }

    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    if(dev_desc == EZB_INVALID_AF_DEVICE_DESC) {
        ESP_LOGE(ZIGBEE_STACK_TAG, "Failed to create Zigbee device descriptor");
        vTaskDelete(NULL);
        return;
    }

     if(ezb_af_device_add_endpoint_desc(dev_desc, switch_ep) != EZB_ERR_NONE ||
         ezb_af_device_desc_register(dev_desc) != EZB_ERR_NONE) {
        ESP_LOGE(ZIGBEE_STACK_TAG, "Failed to register Zigbee endpoint descriptor");
        vTaskDelete(NULL);
        return;
    }

    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    s_stack.endpoint_registered = true;
    xSemaphoreGive(s_stack.state_mutex);

    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(zigbee_stack_app_signal_handler));

    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();

    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

esp_err_t zigbee_stack_init(void) {
    esp_err_t err;

    if(s_stack.initialized) {
        return ESP_OK;
    }

    memset(&s_stack, 0, sizeof(s_stack));

    s_stack.state_mutex = xSemaphoreCreateMutex();
    s_stack.stack_ready_sem = xSemaphoreCreateBinary();
    s_stack.join_sem = xSemaphoreCreateBinary();
    s_stack.leave_sem = xSemaphoreCreateBinary();

    if(s_stack.state_mutex == NULL || s_stack.stack_ready_sem == NULL || s_stack.join_sem == NULL ||
       s_stack.leave_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = nvs_flash_init_partition(ZIGBEE_STORAGE_PARTITION_NAME);
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(
            nvs_flash_erase_partition(ZIGBEE_STORAGE_PARTITION_NAME),
            ZIGBEE_STACK_TAG,
            "Failed to erase Zigbee storage partition");
        err = nvs_flash_init_partition(ZIGBEE_STORAGE_PARTITION_NAME);
    }
    ESP_RETURN_ON_ERROR(err, ZIGBEE_STACK_TAG, "Failed to init Zigbee storage partition");

    if(xTaskCreate(
           zigbee_stack_task,
           ZIGBEE_STACK_TASK_NAME,
           ZIGBEE_STACK_TASK_STACK,
           NULL,
           ZIGBEE_STACK_TASK_PRIO,
           &s_stack.stack_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    if(xSemaphoreTake(s_stack.stack_ready_sem, pdMS_TO_TICKS(10000U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_stack.initialized = true;

    return ESP_OK;
}

esp_err_t zigbee_stack_join(uint8_t channel, uint32_t timeout_ms, zigbee_stack_network_info_t* out_info) {
    uint32_t effective_timeout = timeout_ms != 0U ? timeout_ms : ZIGBEE_STACK_DEFAULT_JOIN_TIMEOUT_MS;
    uint32_t channel_mask;
    TickType_t start_tick;
    uint32_t elapsed_ms;
    uint32_t attempt = 0U;
    bool saw_commissioning_result = false;

    if((channel != 0U && (channel < 11U || channel > 26U)) || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(!s_stack.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if(channel == 0U) {
        channel_mask = 0U;
        for(uint8_t ch = 11U; ch <= 26U; ch++) {
            channel_mask |= ZIGBEE_CHANNEL_TO_MASK(ch);
        }
    } else {
        channel_mask = ZIGBEE_CHANNEL_TO_MASK(channel);
    }

    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    if(s_stack.join_in_progress || s_stack.leave_in_progress) {
        xSemaphoreGive(s_stack.state_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    s_stack.join_in_progress = true;
    s_stack.join_ok = false;
    s_stack.last_steering_status = 0xFFU;
    xSemaphoreGive(s_stack.state_mutex);

    ESP_LOGI(
        ZIGBEE_STACK_TAG,
        "Starting network steering (channel=%u timeout_ms=%u)",
        (unsigned)channel,
        (unsigned)effective_timeout);

    start_tick = xTaskGetTickCount();

    /* Always factory-reset before steering so ZHA sees a fresh device with no
     * stale network credentials.  The LEAVE signal fires regardless of whether
     * we were previously joined, but we wait with a generous timeout and treat
     * a timeout as "not previously joined" and continue regardless. */
    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    /* drain any stale leave signal */
    while(xSemaphoreTake(s_stack.leave_sem, 0U) == pdTRUE) {}
    s_stack.leave_in_progress = true;
    xSemaphoreGive(s_stack.state_mutex);

    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_bdb_reset_via_local_action();
    esp_zigbee_lock_release();

    /* wait for factory-reset signal; ignore timeout (device may not have been joined) */
    xSemaphoreTake(s_stack.leave_sem, pdMS_TO_TICKS(ZIGBEE_PRE_JOIN_RESET_TIMEOUT_MS));

    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    s_stack.leave_in_progress = false;
    s_stack.net.joined = false;
    xSemaphoreGive(s_stack.state_mutex);

    ESP_LOGI(ZIGBEE_STACK_TAG, "Pre-join factory reset complete, starting steering");

    do {
        TickType_t wait_ticks;
        TickType_t attempt_start_tick;
        uint32_t remaining_ms;
        uint32_t attempt_wait_ms;

        attempt++;

        while(xSemaphoreTake(s_stack.join_sem, 0U) == pdTRUE) {
        }

        xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
        s_stack.join_ok = false;
        xSemaphoreGive(s_stack.state_mutex);

        attempt_start_tick = xTaskGetTickCount();

        esp_zigbee_lock_acquire(portMAX_DELAY);
        ezb_bdb_set_scan_duration(6U);
        if(ezb_bdb_set_primary_channel_set(channel_mask) != EZB_ERR_NONE ||
           ezb_bdb_set_secondary_channel_set(channel_mask) != EZB_ERR_NONE ||
           ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING) != EZB_ERR_NONE) {
            esp_zigbee_lock_release();
            xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
            s_stack.join_in_progress = false;
            xSemaphoreGive(s_stack.state_mutex);
            return ESP_FAIL;
        }
        esp_zigbee_lock_release();

        elapsed_ms = (uint32_t)((xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS);
        remaining_ms = (elapsed_ms >= effective_timeout) ? 0U : (effective_timeout - elapsed_ms);
        attempt_wait_ms = remaining_ms;
        if(attempt_wait_ms > ZIGBEE_STEERING_ATTEMPT_MAX_WAIT_MS) {
            attempt_wait_ms = ZIGBEE_STEERING_ATTEMPT_MAX_WAIT_MS;
        }
        wait_ticks = pdMS_TO_TICKS(attempt_wait_ms);

        ESP_LOGI(
            ZIGBEE_STACK_TAG,
            "Steering attempt %u started (max_wait=%u ms)",
            (unsigned)attempt,
            (unsigned)attempt_wait_ms);

        if(xSemaphoreTake(s_stack.join_sem, wait_ticks) == pdTRUE) {
            uint8_t last_status;
            uint32_t attempt_elapsed_ms =
                (uint32_t)((xTaskGetTickCount() - attempt_start_tick) * portTICK_PERIOD_MS);
            saw_commissioning_result = true;

            xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
            if(s_stack.join_ok && s_stack.net.joined) {
                ESP_LOGI(
                    ZIGBEE_STACK_TAG,
                    "Steering attempt %u succeeded in %u ms",
                    (unsigned)attempt,
                    (unsigned)attempt_elapsed_ms);
                zigbee_stack_copy_network_locked(out_info);
                s_stack.join_in_progress = false;
                xSemaphoreGive(s_stack.state_mutex);
                return ESP_OK;
            }
            last_status = s_stack.last_steering_status;
            xSemaphoreGive(s_stack.state_mutex);

            ESP_LOGW(
                ZIGBEE_STACK_TAG,
                "Steering attempt %u failed in %u ms status=0x%02x",
                (unsigned)attempt,
                (unsigned)attempt_elapsed_ms,
                (unsigned)last_status);

            if(last_status == ZIGBEE_BDB_STATUS_NO_NETWORK &&
               attempt >= ZIGBEE_STEERING_MAX_NO_NETWORK_RETRIES) {
                ESP_LOGW(
                    ZIGBEE_STACK_TAG,
                    "Stopping steering early after %u no-network results",
                    (unsigned)attempt);
                break;
            }
        }

        elapsed_ms = (uint32_t)((xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS);
    } while(elapsed_ms < effective_timeout);

    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    s_stack.join_in_progress = false;
    xSemaphoreGive(s_stack.state_mutex);

    return saw_commissioning_result ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

esp_err_t zigbee_stack_leave(uint32_t timeout_ms) {
    uint32_t effective_timeout = timeout_ms != 0U ? timeout_ms : 15000U;

    if(!s_stack.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    if(!s_stack.net.joined) {
        s_stack.leave_ok = true;
        xSemaphoreGive(s_stack.state_mutex);
        return ESP_OK;
    }

    if(s_stack.join_in_progress || s_stack.leave_in_progress) {
        xSemaphoreGive(s_stack.state_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    s_stack.leave_in_progress = true;
    s_stack.leave_ok = false;
    xSemaphoreGive(s_stack.state_mutex);

    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_bdb_reset_via_local_action();
    esp_zigbee_lock_release();

    if(xSemaphoreTake(s_stack.leave_sem, pdMS_TO_TICKS(effective_timeout)) != pdTRUE) {
        xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
        s_stack.leave_in_progress = false;
        xSemaphoreGive(s_stack.state_mutex);
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    if(!s_stack.leave_ok || s_stack.net.joined) {
        xSemaphoreGive(s_stack.state_mutex);
        return ESP_FAIL;
    }
    xSemaphoreGive(s_stack.state_mutex);

    return ESP_OK;
}

esp_err_t zigbee_stack_get_network_info(zigbee_stack_network_info_t* out_info) {
    if(out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(!s_stack.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    zigbee_stack_refresh_network_locked();
    zigbee_stack_copy_network_locked(out_info);
    xSemaphoreGive(s_stack.state_mutex);

    return ESP_OK;
}

/* One-shot channel scan.  Does a factory reset, kicks off a single BDB
 * steering attempt on all channels, and waits up to 20 s for a BDB response.
 * If the device accidentally joins an open network during the scan, it
 * immediately leaves (the caller only wants diagnostics, not a real join).
 * Returns:
 *   ESP_OK        – got a BDB response (out->bdb_status and out->joined set)
 *   ESP_ERR_TIMEOUT – no BDB response received (possible radio fault)
 */
esp_err_t zigbee_stack_scan(uint8_t channel, zigbee_stack_scan_result_t* out) {
    uint32_t channel_mask;
    bool joined_during_scan = false;

    if(out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(!s_stack.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(out, 0, sizeof(*out));

    if(channel != 0U && (channel < 11U || channel > 26U)) {
        return ESP_ERR_INVALID_ARG;
    }

    if(channel == 0U) {
        channel_mask = 0U;
        for(uint8_t ch = 11U; ch <= 26U; ch++) {
            channel_mask |= ZIGBEE_CHANNEL_TO_MASK(ch);
        }
    } else {
        channel_mask = ZIGBEE_CHANNEL_TO_MASK(channel);
    }

    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    if(s_stack.join_in_progress || s_stack.leave_in_progress) {
        xSemaphoreGive(s_stack.state_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_stack.join_in_progress = true;
    s_stack.join_ok = false;
    s_stack.last_steering_status = 0xFFU;
    xSemaphoreGive(s_stack.state_mutex);

    /* factory reset first (same as join) */
    while(xSemaphoreTake(s_stack.leave_sem, 0U) == pdTRUE) {}
    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    s_stack.leave_in_progress = true;
    xSemaphoreGive(s_stack.state_mutex);

    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_bdb_reset_via_local_action();
    esp_zigbee_lock_release();

    xSemaphoreTake(s_stack.leave_sem, pdMS_TO_TICKS(ZIGBEE_PRE_JOIN_RESET_TIMEOUT_MS));

    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    s_stack.leave_in_progress = false;
    s_stack.net.joined = false;
    xSemaphoreGive(s_stack.state_mutex);

    ESP_LOGI(ZIGBEE_STACK_TAG, "Scan: factory reset done, scanning all channels");

    /* drain any stale join signal */
    while(xSemaphoreTake(s_stack.join_sem, 0U) == pdTRUE) {}

    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_bdb_set_scan_duration(6U);
    bool start_ok = (ezb_bdb_set_primary_channel_set(channel_mask) == EZB_ERR_NONE &&
                     ezb_bdb_set_secondary_channel_set(channel_mask) == EZB_ERR_NONE &&
                     ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING) == EZB_ERR_NONE);
    esp_zigbee_lock_release();

    if(!start_ok) {
        xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
        s_stack.join_in_progress = false;
        xSemaphoreGive(s_stack.state_mutex);
        return ESP_FAIL;
    }

    /* wait up to 20 s for one BDB steering response */
    if(xSemaphoreTake(s_stack.join_sem, pdMS_TO_TICKS(20000U)) == pdTRUE) {
        out->got_bdb_response = true;

        xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
        out->bdb_status = s_stack.last_steering_status;
        out->joined     = s_stack.join_ok && s_stack.net.joined;
        if(out->joined) {
            joined_during_scan = true;
            zigbee_stack_copy_network_locked(&out->net);
        }
        s_stack.join_in_progress = false;
        xSemaphoreGive(s_stack.state_mutex);

        ESP_LOGI(ZIGBEE_STACK_TAG, "Scan got BDB response status=0x%02x joined=%d",
                 (unsigned)out->bdb_status, (int)out->joined);
    } else {
        out->got_bdb_response = false;
        xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
        s_stack.join_in_progress = false;
        xSemaphoreGive(s_stack.state_mutex);
        ESP_LOGW(ZIGBEE_STACK_TAG, "Scan timed out - no BDB response");
        return ESP_ERR_TIMEOUT;
    }

    /* If we joined during the scan, leave immediately - caller didn't ask to join */
    if(joined_during_scan) {
        ESP_LOGI(ZIGBEE_STACK_TAG, "Scan joined a network unexpectedly, leaving");
        zigbee_stack_leave(5000U);
    }

    return ESP_OK;
}

esp_err_t zigbee_stack_trigger_button(uint8_t button_index) {
    ezb_err_t zerr = EZB_ERR_INV_ARG;
    ezb_zcl_on_off_cmd_t cmd = {0};

    if(!s_stack.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_stack.state_mutex, portMAX_DELAY);
    bool joined = s_stack.net.joined;
    bool endpoint_registered = s_stack.endpoint_registered;
    xSemaphoreGive(s_stack.state_mutex);

    if(!joined || !endpoint_registered) {
        return ESP_ERR_INVALID_STATE;
    }

    ezb_address_set_short(&cmd.cmd_ctrl.dst_addr, 0xFFFFU);
    cmd.cmd_ctrl.dst_ep = ZIGBEE_SWITCH_ENDPOINT;
    cmd.cmd_ctrl.src_ep = ZIGBEE_SWITCH_ENDPOINT;
    cmd.cmd_ctrl.dis_default_rsp = true;

    esp_zigbee_lock_acquire(portMAX_DELAY);
    switch(button_index % 3U) {
    case 0U:
        zerr = ezb_zcl_on_off_toggle_cmd_req(&cmd);
        break;
    case 1U:
        zerr = ezb_zcl_on_off_on_cmd_req(&cmd);
        break;
    default:
        zerr = ezb_zcl_on_off_off_cmd_req(&cmd);
        break;
    }
    esp_zigbee_lock_release();

    return esp_zigbee_err_to_esp(zerr);
}
