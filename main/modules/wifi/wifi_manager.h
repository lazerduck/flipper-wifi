#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef void (*wifi_scan_result_writer_t)(const char *line, void *context);

typedef enum {
	WIFI_MANAGER_MODE_IDLE = 0,
	WIFI_MANAGER_MODE_CONNECTED,
	WIFI_MANAGER_MODE_PROMISCUOUS,
} wifi_manager_mode_t;

typedef enum {
	WIFI_MANAGER_ACTION_NONE = 0,
	WIFI_MANAGER_ACTION_SCANNING,
	WIFI_MANAGER_ACTION_CONNECTING,
	WIFI_MANAGER_ACTION_DISCONNECTING,
	WIFI_MANAGER_ACTION_DISCOVERING,
	WIFI_MANAGER_ACTION_RESOLVING_MDNS,
	WIFI_MANAGER_ACTION_REQUESTING_HTTP,
	WIFI_MANAGER_ACTION_ENTERING_PROMISCUOUS,
	WIFI_MANAGER_ACTION_EXITING_PROMISCUOUS,
	WIFI_MANAGER_ACTION_SURVEYING,
	WIFI_MANAGER_ACTION_WATCHING,
	WIFI_MANAGER_ACTION_BEACONING,
} wifi_manager_action_t;

typedef struct {
	wifi_manager_mode_t mode;
	wifi_manager_action_t action;
	bool has_ip;
	char ssid[33];
	int last_disconnect_reason;
} wifi_manager_status_t;

typedef void (*wifi_manager_status_callback_t)(const wifi_manager_status_t *status, void *context);

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_scan_aps(wifi_scan_result_writer_t write_line, void *context);
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_disconnect(void);
esp_err_t wifi_manager_enter_promiscuous(uint8_t channel);
esp_err_t wifi_manager_exit_promiscuous(void);
esp_err_t wifi_manager_get_status(wifi_manager_status_t *status);
esp_err_t wifi_manager_refresh_status(wifi_manager_status_t *status);
esp_err_t wifi_manager_set_action(wifi_manager_action_t action);
void wifi_manager_clear_action(void);
void wifi_manager_set_status_callback(wifi_manager_status_callback_t callback, void *context);
const char *wifi_manager_mode_to_string(wifi_manager_mode_t mode);
const char *wifi_manager_action_to_string(wifi_manager_action_t action);
const char *wifi_manager_state_to_string(wifi_manager_mode_t mode, wifi_manager_action_t action);