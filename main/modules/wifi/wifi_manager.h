#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef void (*wifi_scan_result_writer_t)(const char *line, void *context);

typedef enum {
	WIFI_MANAGER_STATE_IDLE = 0,
	WIFI_MANAGER_STATE_SCANNING,
	WIFI_MANAGER_STATE_CONNECTING,
	WIFI_MANAGER_STATE_CONNECTED,
} wifi_manager_state_t;

typedef struct {
	wifi_manager_state_t state;
	bool has_ip;
	char ssid[33];
	int last_disconnect_reason;
} wifi_manager_status_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_scan_aps(wifi_scan_result_writer_t write_line, void *context);
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_disconnect(void);
esp_err_t wifi_manager_get_status(wifi_manager_status_t *status);
esp_err_t wifi_manager_refresh_status(wifi_manager_status_t *status);
const char *wifi_manager_state_to_string(wifi_manager_state_t state);