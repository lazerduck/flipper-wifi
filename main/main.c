#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sdkconfig.h"

#include "command/command_context.h"
#include "command/command_router.h"

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "modules/status_led/status_led.h"
#include "modules/settings/settings_manager.h"
#include "modules/system/system_mode_manager.h"
#include "modules/wifi/wifi_manager.h"
#include "modules/zigbee/zigbee_manager.h"

#define COMMAND_UART_NUM ((uart_port_t)CONFIG_FLIPPER_COMMAND_UART_PORT)
#define COMMAND_UART_BAUD_RATE CONFIG_FLIPPER_COMMAND_UART_BAUD_RATE
#define COMMAND_UART_TX_PIN CONFIG_FLIPPER_COMMAND_UART_TX_PIN
#define COMMAND_UART_RX_PIN CONFIG_FLIPPER_COMMAND_UART_RX_PIN
#define COMMAND_RX_BUFFER_SIZE 1024
#define COMMAND_TX_BUFFER_SIZE 1024
#define COMMAND_MAX_LENGTH 256
#define COMMAND_POLL_TICKS pdMS_TO_TICKS(100)

typedef enum {
	APP_STATE_LISTENING = 0,
	APP_STATE_PROCESSING,
} app_state_t;

static esp_err_t command_uart_init(void)
{
	const uart_config_t uart_config = {
		.baud_rate = COMMAND_UART_BAUD_RATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};

	esp_err_t err = uart_driver_install(
		COMMAND_UART_NUM,
		COMMAND_RX_BUFFER_SIZE,
		COMMAND_TX_BUFFER_SIZE,
		0,
		NULL,
		0);
	if (err != ESP_OK) {
		return err;
	}

	err = uart_param_config(COMMAND_UART_NUM, &uart_config);
	if (err != ESP_OK) {
		return err;
	}

	return uart_set_pin(
		COMMAND_UART_NUM,
		COMMAND_UART_TX_PIN,
		COMMAND_UART_RX_PIN,
		UART_PIN_NO_CHANGE,
		UART_PIN_NO_CHANGE);
}

static SemaphoreHandle_t s_command_uart_tx_mutex = NULL;

static void command_uart_write(const char *response)
{
	if (response == NULL) {
		return;
	}

	if (s_command_uart_tx_mutex != NULL) {
		xSemaphoreTake(s_command_uart_tx_mutex, portMAX_DELAY);
	}

	uart_write_bytes(COMMAND_UART_NUM, response, strlen(response));

	if (s_command_uart_tx_mutex != NULL) {
		xSemaphoreGive(s_command_uart_tx_mutex);
	}
}

static void process_command_buffer(char *command_buffer)
{
	command_context_t command_context = {
		.write_response = command_uart_write,
	};
	size_t index = 0U;

	if (command_buffer[0] == '\0') {
		return;
	}

	if (command_buffer[0] < 'A' || command_buffer[0] > 'Z') {
		return;
	}

	while (command_buffer[index] != '\0') {
		const char current = command_buffer[index++];

		if (current < 0x20 || current > 0x7e) {
			return;
		}
	}

	command_router_dispatch(command_buffer, &command_context);
}

static void app_status_led_wifi_state_changed(const wifi_manager_status_t *status, void *context)
{
	(void)context;

	if (!status_led_is_ready()) {
		return;
	}

	status_led_apply_wifi_status(status);
}

void app_main(void)
{
	char command_buffer[COMMAND_MAX_LENGTH];
	size_t command_length = 0;
	app_state_t state = APP_STATE_LISTENING;
	esp_err_t led_err;

	memset(command_buffer, 0, sizeof(command_buffer));
	esp_log_level_set("*", ESP_LOG_INFO);
	esp_log_level_set("wifi_discovery", ESP_LOG_INFO);
	esp_log_level_set("sd_card", ESP_LOG_DEBUG);
	esp_log_level_set("sdmmc_cmd", ESP_LOG_DEBUG);
	esp_log_level_set("sdmmc_common", ESP_LOG_DEBUG);
	esp_log_level_set("sdspi_host", ESP_LOG_DEBUG);
	esp_log_level_set("sdspi_transaction", ESP_LOG_DEBUG);
	esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_DEBUG);
	esp_log_level_set("diskio_sdmmc", ESP_LOG_DEBUG);
	ESP_LOGI("main", "app_main init start");

	led_err = status_led_init();

	settings_manager_init();
	ESP_LOGI("main", "settings initialized");
	ESP_ERROR_CHECK(system_mode_manager_init());
	ESP_LOGI("main", "system mode manager initialized");

	ESP_ERROR_CHECK(command_uart_init());
	ESP_LOGI("main", "command UART initialized port=%d tx=%d rx=%d baud=%d", (int)COMMAND_UART_NUM, COMMAND_UART_TX_PIN, COMMAND_UART_RX_PIN, COMMAND_UART_BAUD_RATE);
	s_command_uart_tx_mutex = xSemaphoreCreateMutex();

	{
		system_mode_t active_mode = SYSTEM_MODE_WIFI;
		esp_err_t mode_err;
		esp_err_t stack_err = ESP_OK;
		char mode_line[96];

		mode_err = system_mode_manager_get_active_mode(&active_mode);
		if (mode_err != ESP_OK) {
			ESP_LOGW("main", "system mode query failed: 0x%X; defaulting to WIFI", (unsigned)mode_err);
			active_mode = SYSTEM_MODE_WIFI;
		}

		ESP_LOGI("main", "active mode: %s", system_mode_manager_mode_to_string(active_mode));

		if (active_mode == SYSTEM_MODE_WIFI) {
			stack_err = wifi_manager_init();
			if (stack_err == ESP_OK) {
				ESP_LOGI("main", "wifi manager initialized");
				if (led_err == ESP_OK) {
					wifi_manager_set_status_callback(app_status_led_wifi_state_changed, NULL);
				}
			}
		} else {
			stack_err = zigbee_manager_init();
			if (stack_err == ESP_OK) {
				ESP_LOGI("main", "zigbee manager initialized");
			}
		}

		snprintf(
			mode_line,
			sizeof(mode_line),
			"BOOT_MODE mode=%s init=%s code=0x%X\n",
			system_mode_manager_mode_to_string(active_mode),
			stack_err == ESP_OK ? "ok" : "failed",
			(unsigned)stack_err);
		command_uart_write(mode_line);
	}

	command_uart_write("READY\n");
	ESP_LOGI("main", "command interface ready");

	while (true) {
		uint8_t byte = 0;
		int bytes_read = uart_read_bytes(COMMAND_UART_NUM, &byte, 1, COMMAND_POLL_TICKS);

		if (bytes_read <= 0) {
			continue;
		}

		if (state == APP_STATE_LISTENING) {
			if (byte == '\n') {
				command_buffer[command_length] = '\0';
				state = APP_STATE_PROCESSING;
			} else if (command_length < (sizeof(command_buffer) - 1U)) {
				command_buffer[command_length++] = (char)byte;
			} else {
				command_length = 0;
				memset(command_buffer, 0, sizeof(command_buffer));
				command_uart_write("ERR COMMAND_TOO_LONG\n");
			}
		}

		if (state == APP_STATE_PROCESSING) {
			process_command_buffer(command_buffer);
			command_length = 0;
			memset(command_buffer, 0, sizeof(command_buffer));
			state = APP_STATE_LISTENING;
		}
	}
}