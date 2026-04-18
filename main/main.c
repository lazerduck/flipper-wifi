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
#include "freertos/task.h"
#include "modules/status_led/status_led.h"
#include "modules/wifi/wifi_manager.h"

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

static void command_uart_write(const char *response)
{
	if (response == NULL) {
		return;
	}

	uart_write_bytes(COMMAND_UART_NUM, response, strlen(response));
}

static void process_command_buffer(char *command_buffer)
{
	command_context_t command_context = {
		.write_response = command_uart_write,
	};

	if (command_buffer[0] == '\0') {
		return;
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
	esp_log_level_set("*", ESP_LOG_NONE);
	esp_log_level_set("wifi_discovery", ESP_LOG_INFO);

	led_err = status_led_init();
	if (led_err == ESP_OK) {
		wifi_manager_set_status_callback(app_status_led_wifi_state_changed, NULL);
	}

	ESP_ERROR_CHECK(command_uart_init());
	ESP_ERROR_CHECK(wifi_manager_init());

	command_uart_write("READY\n");

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