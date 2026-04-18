#include "modules/wifi/wifi_manager.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define WIFI_SCAN_MAX_APS 20
#define WIFI_SCAN_LINE_MAX_LENGTH 160

static bool s_wifi_initialized = false;
static bool s_has_ip = false;
static wifi_manager_state_t s_state = WIFI_MANAGER_STATE_IDLE;
static char s_connected_ssid[33];
static int s_last_disconnect_reason = 0;
static wifi_manager_state_callback_t s_state_callback = NULL;
static void *s_state_callback_context = NULL;

static void notify_state_changed(void)
{
    if (s_state_callback != NULL) {
        s_state_callback(s_state, s_state_callback_context);
    }
}

static void set_state(wifi_manager_state_t state)
{
    if (s_state == state) {
        return;
    }

    s_state = state;
    notify_state_changed();
}

static void copy_ssid(char *destination, size_t destination_size, const uint8_t *source, size_t source_length)
{
    size_t copy_length = source_length;

    if (destination == NULL || destination_size == 0 || source == NULL) {
        return;
    }

    if (copy_length >= destination_size) {
        copy_length = destination_size - 1U;
    }

    memcpy(destination, source, copy_length);
    destination[copy_length] = '\0';
}

static void clear_connected_ssid(void)
{
    memset(s_connected_ssid, 0, sizeof(s_connected_ssid));
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_CONNECTED) {
            wifi_event_sta_connected_t *connected_event = (wifi_event_sta_connected_t *)event_data;

            set_state(WIFI_MANAGER_STATE_CONNECTING);
            s_last_disconnect_reason = 0;
            s_has_ip = false;
            copy_ssid(s_connected_ssid, sizeof(s_connected_ssid), connected_event->ssid, connected_event->ssid_len);
            return;
        }

        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *disconnected_event = (wifi_event_sta_disconnected_t *)event_data;

            s_has_ip = false;
            set_state(WIFI_MANAGER_STATE_IDLE);
            s_last_disconnect_reason = disconnected_event->reason;
            clear_connected_ssid();
            return;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_has_ip = true;
        set_state(WIFI_MANAGER_STATE_CONNECTED);
        s_last_disconnect_reason = 0;
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        s_has_ip = false;
        if (s_state == WIFI_MANAGER_STATE_CONNECTED) {
            set_state(WIFI_MANAGER_STATE_CONNECTING);
        }
    }
}

static void populate_status(wifi_manager_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->state = s_state;
    status->has_ip = s_has_ip;
    status->last_disconnect_reason = s_last_disconnect_reason;
    memset(status->ssid, 0, sizeof(status->ssid));
    memcpy(status->ssid, s_connected_ssid, sizeof(status->ssid));
}

static const char *auth_mode_to_string(wifi_auth_mode_t auth_mode)
{
    switch (auth_mode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA_WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2_WPA3";
    default:
        return "UNKNOWN";
    }
}

const char *wifi_manager_state_to_string(wifi_manager_state_t state)
{
    switch (state) {
    case WIFI_MANAGER_STATE_IDLE:
        return "IDLE";
    case WIFI_MANAGER_STATE_SCANNING:
        return "SCANNING";
    case WIFI_MANAGER_STATE_CONNECTING:
        return "CONNECTING";
    case WIFI_MANAGER_STATE_CONNECTED:
        return "CONNECTED";
    default:
        return "UNKNOWN";
    }
}

static esp_err_t wifi_stack_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }

        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    if (s_wifi_initialized) {
        return ESP_OK;
    }

    esp_err_t err = wifi_stack_init();
    if (err != ESP_OK) {
        return err;
    }

    s_wifi_initialized = true;
    s_state = WIFI_MANAGER_STATE_IDLE;
    s_has_ip = false;
    s_last_disconnect_reason = 0;
    clear_connected_ssid();
    notify_state_changed();
    return ESP_OK;
}

esp_err_t wifi_manager_get_status(wifi_manager_status_t *status)
{
    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    populate_status(status);
    return ESP_OK;
}

esp_err_t wifi_manager_refresh_status(wifi_manager_status_t *status)
{
    wifi_ap_record_t ap_info;

    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK) {
        copy_ssid(s_connected_ssid, sizeof(s_connected_ssid), ap_info.ssid, sizeof(ap_info.ssid));
        if (s_has_ip) {
            set_state(WIFI_MANAGER_STATE_CONNECTED);
        } else if (s_state != WIFI_MANAGER_STATE_SCANNING) {
            set_state(WIFI_MANAGER_STATE_CONNECTING);
        }
    } else if (err == ESP_ERR_WIFI_NOT_CONNECT) {
        s_has_ip = false;
        if (s_state != WIFI_MANAGER_STATE_SCANNING) {
            set_state(WIFI_MANAGER_STATE_IDLE);
        }
        clear_connected_ssid();
        err = ESP_OK;
    } else {
        return err;
    }

    populate_status(status);
    return ESP_OK;
}

esp_err_t wifi_manager_scan_aps(wifi_scan_result_writer_t write_line, void *context)
{
    wifi_ap_record_t ap_records[WIFI_SCAN_MAX_APS];
    uint16_t ap_count = WIFI_SCAN_MAX_APS;
    uint16_t total_ap_count = 0;
    char line[WIFI_SCAN_LINE_MAX_LENGTH];
    wifi_manager_state_t previous_state;

    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    previous_state = s_state;
    set_state(WIFI_MANAGER_STATE_SCANNING);

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        set_state(previous_state);
        return err;
    }

    err = esp_wifi_scan_get_ap_num(&total_ap_count);
    if (err != ESP_OK) {
        set_state(previous_state);
        return err;
    }

    if (total_ap_count < ap_count) {
        ap_count = total_ap_count;
    }

    if (ap_count > 0) {
        err = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        if (err != ESP_OK) {
            set_state(previous_state);
            return err;
        }
    }

    if (write_line != NULL) {
        snprintf(line, sizeof(line), "SCAN_COUNT %u\n", total_ap_count);
        write_line(line, context);

        for (uint16_t index = 0; index < ap_count; ++index) {
            const wifi_ap_record_t *record = &ap_records[index];
            snprintf(
                line,
                sizeof(line),
                "AP %s RSSI %d AUTH %s CH %u\n",
                (const char *)record->ssid,
                record->rssi,
                auth_mode_to_string(record->authmode),
                record->primary);
            write_line(line, context);
        }

        if (total_ap_count > ap_count) {
            snprintf(line, sizeof(line), "SCAN_TRUNCATED %u\n", total_ap_count - ap_count);
            write_line(line, context);
        }

        write_line("SCAN_DONE\n", context);
    }

    set_state(previous_state);

    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = { 0 };
    size_t ssid_length;
    size_t password_length;
    esp_err_t err;

    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ssid_length = strlen(ssid);
    password_length = strlen(password);
    if (ssid_length == 0 || ssid_length >= sizeof(wifi_config.sta.ssid) || password_length >= sizeof(wifi_config.sta.password)) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(wifi_config.sta.ssid, ssid, ssid_length);
    memcpy(wifi_config.sta.password, password, password_length);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    set_state(WIFI_MANAGER_STATE_CONNECTING);
    s_has_ip = false;
    s_last_disconnect_reason = 0;
    clear_connected_ssid();
    memcpy(s_connected_ssid, ssid, ssid_length);
    s_connected_ssid[ssid_length] = '\0';

    return esp_wifi_connect();
}

esp_err_t wifi_manager_disconnect(void)
{
    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_wifi_disconnect();
}

void wifi_manager_set_state_callback(wifi_manager_state_callback_t callback, void *context)
{
    s_state_callback = callback;
    s_state_callback_context = context;

    if (callback != NULL) {
        callback(s_state, context);
    }
}