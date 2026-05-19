#include "uart.h"
#include <expansion/expansion.h>
#include <string.h>

#define WORKER_EVENT_RX_DATA (1u << 0)
#define WORKER_EVENT_STOP    (1u << 1)

struct AppUart {
    FuriHalSerialHandle* handle;
    FuriStreamBuffer*    rx_stream;
    FuriThread*          worker_thread;
    FuriThreadId         worker_thread_id;
    UartLineCallback     line_cb;
    void*                line_cb_ctx;
};

/* Called from the UART RX ISR — keep it minimal and ISR-safe. */
static void uart_rx_isr(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    AppUart* uart = ctx;
    if(event & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        /* timeout=0: does not block, safe to call with a short burst from ISR */
        furi_stream_buffer_send(uart->rx_stream, &byte, 1, 0);
        furi_thread_flags_set(uart->worker_thread_id, WORKER_EVENT_RX_DATA);
    }
}

/* Worker thread: accumulates bytes into lines, fires line_cb in task context. */
static int32_t uart_worker(void* ctx) {
    AppUart* uart = ctx;
    char     buf[UART_RX_BUF_SIZE];
    size_t   len = 0;

    while(true) {
        uint32_t flags = furi_thread_flags_wait(
            WORKER_EVENT_RX_DATA | WORKER_EVENT_STOP, FuriFlagWaitAny, FuriWaitForever);

        if(flags & WORKER_EVENT_STOP) break;

        if(flags & WORKER_EVENT_RX_DATA) {
            uint8_t byte;
            while(furi_stream_buffer_receive(uart->rx_stream, &byte, 1, 0) == 1) {
                if(byte == '\n') {
                    /* strip trailing CR if present */
                    if(len > 0 && buf[len - 1] == '\r') len--;
                    buf[len] = '\0';
                    if(len > 0 && uart->line_cb) {
                        uart->line_cb(buf, uart->line_cb_ctx);
                    }
                    len = 0;
                } else if(len < UART_RX_BUF_SIZE - 1) {
                    buf[len++] = byte;
                }
            }
        }
    }
    return 0;
}

AppUart* app_uart_alloc(UartLineCallback cb, void* context) {
    AppUart* uart     = malloc(sizeof(AppUart));
    uart->line_cb     = cb;
    uart->line_cb_ctx = context;
    uart->rx_stream   = furi_stream_buffer_alloc(UART_RX_BUF_SIZE * 2, 1);

    uart->worker_thread = furi_thread_alloc_ex("UartWorker", 1024, uart_worker, uart);
    furi_thread_start(uart->worker_thread);
    uart->worker_thread_id = furi_thread_get_id(uart->worker_thread);

    /* Tell the expansion module to release the USART before we acquire it */
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);
    furi_record_close(RECORD_EXPANSION);

    uart->handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_check(uart->handle);
    furi_hal_serial_init(uart->handle, UART_BAUD_RATE);
    furi_hal_serial_async_rx_start(uart->handle, uart_rx_isr, uart, false);

    return uart;
}

void app_uart_set_callback(AppUart* uart, UartLineCallback cb, void* context) {
    uart->line_cb     = cb;
    uart->line_cb_ctx = context;
}

void app_uart_free(AppUart* uart) {
    /* Stop serial first so no more ISR callbacks fire */
    furi_hal_serial_async_rx_stop(uart->handle);
    furi_hal_serial_deinit(uart->handle);
    furi_hal_serial_control_release(uart->handle);

    /* Hand the USART back to the expansion module */
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);

    /* Signal worker to exit and wait */
    furi_thread_flags_set(uart->worker_thread_id, WORKER_EVENT_STOP);
    furi_thread_join(uart->worker_thread);
    furi_thread_free(uart->worker_thread);

    furi_stream_buffer_free(uart->rx_stream);
    free(uart);
}

void app_uart_send(AppUart* uart, const char* line) {
    furi_hal_serial_tx(uart->handle, (const uint8_t*)line, strlen(line));
}
