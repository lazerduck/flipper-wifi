#include "uart.h"
#include "parser.h"
#include "router.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UART_RX_BUF_SIZE 512

static app_state_t *s_state;

static void uart_reader_task(void *arg)
{
    // TODO: read bytes into a line buffer; on \n call parser_parse() then router_dispatch()
    (void)arg;
    vTaskDelete(NULL);
}

void uart_init(app_state_t *state)
{
    // TODO: configure UART1 with pins and baud from Kconfig; spawn uart_reader_task
    s_state = state;
}

void uart_send(const char *line)
{
    // TODO: write all bytes of line to UART1 TX
}

void uart_send_ok(void)
{
    // TODO: transmit "S\n"
}

void uart_send_ok_with(const char *fields)
{
    // TODO: transmit "S <fields>\n"
}

void uart_send_stream_start(void)
{
    // TODO: transmit "S STREAM\n"
}

void uart_send_data(const char *fields)
{
    // TODO: transmit "DATA <fields>\n"
}

void uart_send_end(void)
{
    // TODO: transmit "END\n"
}

void uart_send_stopped(void)
{
    // TODO: transmit "STOPPED\n"
}

void uart_send_error(const char *code)
{
    // TODO: transmit "E code=\"<code>\"\n"
}
