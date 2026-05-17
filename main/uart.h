#pragma once

#include "types.h"

/*
 * uart.h — UART send/receive over UART1.
 *
 * uart_init() starts a background task that reads lines from the Flipper
 * and calls router_dispatch() for each one.  All uart_send_* helpers write
 * a single line (terminated with \n) back to the Flipper.  They follow the
 * wire format defined in Architecture/UART.MD exactly.
 */

/* Start the UART1 driver and spawn the line-reader task. */
void uart_init(app_state_t *state);

/* Send a raw pre-formatted line (caller must include the trailing \n). */
void uart_send(const char *line);

/* ── Response helpers ────────────────────────────────────────────────────── */

/* S\n  — success with no fields. */
void uart_send_ok(void);

/* S <fields>\n  — success carrying one or more key="value" pairs.
 * Example: uart_send_ok_with("msg=\"PONG\""); */
void uart_send_ok_with(const char *fields);

/* S STREAM\n  — announces that DATA lines will follow. */
void uart_send_stream_start(void);

/* DATA <fields>\n  — one record inside an open stream. */
void uart_send_data(const char *fields);

/* END\n  — stream finished naturally. */
void uart_send_end(void);

/* STOPPED\n  — stream was cut short by SYS_STOP. */
void uart_send_stopped(void);

/* E code="<code>"\n  — command failed with the given error code string.
 * Example: uart_send_error("ERR_WRONG_MODE"); */
void uart_send_error(const char *code);
