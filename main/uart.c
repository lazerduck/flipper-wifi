#include "uart.h"
#include "parser.h"
#include "router.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"

#define UART_RX_BUF_SIZE  512
#define UART_TX_BUF_SIZE  512
#define UART_LINE_BUF_SIZE 256

static app_state_t *s_state;

static void uart_reader_task(void *arg)
{
    (void)arg;
    uint8_t byte;
    char    line_buf[UART_LINE_BUF_SIZE];
    int     line_len = 0;

    while (1) {
        int n = uart_read_bytes(CONFIG_FUSE_UART_PORT, &byte, 1, portMAX_DELAY);
        if (n <= 0) {
            continue;
        }
        if (byte == '\r') {
            continue; /* tolerate \r\n — only \n triggers dispatch */
        }
        if (byte == '\n') {
            line_buf[line_len] = '\0';
            if (line_len > 0) {
                parsed_cmd_t cmd;
                if (parser_parse(line_buf, &cmd)) {
                    router_dispatch(&cmd, s_state);
                    parser_free(&cmd);
                }
            }
            line_len = 0;
        } else {
            if (line_len < UART_LINE_BUF_SIZE - 1) {
                line_buf[line_len++] = (char)byte;
            }
            /* If buffer is full, keep discarding until the next \n */
        }
    }
}

void uart_init(app_state_t *state)
{
    s_state = state;

    const uart_config_t uart_cfg = {
        .baud_rate  = CONFIG_FUSE_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(CONFIG_FUSE_UART_PORT,
                        UART_RX_BUF_SIZE * 2, UART_TX_BUF_SIZE,
                        0, NULL, 0);
    uart_param_config(CONFIG_FUSE_UART_PORT, &uart_cfg);
    uart_set_pin(CONFIG_FUSE_UART_PORT,
                 CONFIG_FUSE_UART_TX_PIN, CONFIG_FUSE_UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    xTaskCreate(uart_reader_task, "uart_reader", 4096, NULL, 5, NULL);
}

void uart_send(const char *line)
{
    uart_write_bytes(CONFIG_FUSE_UART_PORT, line, strlen(line));
}

void uart_send_ok(void)
{
    uart_send("S\n");
}

void uart_send_ok_with(const char *fields)
{
    char buf[UART_LINE_BUF_SIZE];
    snprintf(buf, sizeof(buf), "S %s\n", fields);
    uart_send(buf);
}

void uart_send_stream_start(void)
{
    uart_send("S STREAM\n");
}

void uart_send_data(const char *fields)
{
    char buf[UART_LINE_BUF_SIZE];
    snprintf(buf, sizeof(buf), "DATA %s\n", fields);
    uart_send(buf);
}

void uart_send_end(void)
{
    uart_send("END\n");
}

void uart_send_stopped(void)
{
    uart_send("STOPPED\n");
}

void uart_send_error(const char *code)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "E code=\"%s\"\n", code);
    uart_send(buf);
}
