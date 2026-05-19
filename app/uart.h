#pragma once

#include <furi.h>
#include <furi_hal.h>

#define UART_BAUD_RATE   115200
#define UART_RX_BUF_SIZE 256

typedef struct AppUart AppUart;

/* Called from the UART worker thread (task context — safe to post ViewDispatcher events). */
typedef void (*UartLineCallback)(const char* line, void* context);

AppUart* app_uart_alloc(UartLineCallback cb, void* context);
void     app_uart_free(AppUart* uart);

/* Swap the line callback at runtime (e.g. when entering a new scene). */
void     app_uart_set_callback(AppUart* uart, UartLineCallback cb, void* context);

/* Send a pre-formatted line; caller must include the trailing '\n'. */
void app_uart_send(AppUart* uart, const char* line);
