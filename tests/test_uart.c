#include "unity.h"
#include "uart.h"

#include <string.h>
#include <stdint.h>
#include "driver/uart.h"
#include "freertos/task.h"

/* ─── ESP-IDF UART driver stubs ──────────────────────────────────────────── *
 * uart_write_bytes captures output so tests can assert wire format.          *
 * All other driver calls are no-ops — uart_init() is never called in these   *
 * tests, so they don't even need to return sensible values.                  */

static char s_tx_buf[512];
static int  s_tx_len;

int uart_write_bytes(uart_port_t port, const void *src, size_t size)
{
    (void)port;
    if (s_tx_len + (int)size < (int)sizeof(s_tx_buf)) {
        memcpy(s_tx_buf + s_tx_len, src, size);
        s_tx_len += (int)size;
    }
    return (int)size;
}

esp_err_t uart_driver_install(uart_port_t p, int rx, int tx, int q,
                               void *qh, int flags)
{ (void)p; (void)rx; (void)tx; (void)q; (void)qh; (void)flags; return 0; }

esp_err_t uart_param_config(uart_port_t p, const uart_config_t *cfg)
{ (void)p; (void)cfg; return 0; }

esp_err_t uart_set_pin(uart_port_t p, int tx, int rx, int rts, int cts)
{ (void)p; (void)tx; (void)rx; (void)rts; (void)cts; return 0; }

int uart_read_bytes(uart_port_t p, void *buf, uint32_t len, TickType_t ticks)
{ (void)p; (void)buf; (void)len; (void)ticks; return 0; }

/* ─── FreeRTOS task stub ─────────────────────────────────────────────────── */

BaseType_t xTaskCreate(void (*fn)(void *), const char *name,
                       uint32_t stack, void *param,
                       UBaseType_t prio, TaskHandle_t *hdl)
{ (void)fn; (void)name; (void)stack; (void)param; (void)prio; (void)hdl;
  return pdTRUE; }

/* ─── parser / router stubs ──────────────────────────────────────────────── *
 * uart_reader_task calls these; they are not under test here.               */
#include "parser.h"
#include "router.h"

bool        parser_parse(const char *l, parsed_cmd_t *o) { (void)l; (void)o; return false; }
const char *parser_get_arg(const parsed_cmd_t *c, const char *k) { (void)c; (void)k; return NULL; }
void        parser_free(parsed_cmd_t *c)                  { (void)c; }
void        router_dispatch(const parsed_cmd_t *c, app_state_t *s) { (void)c; (void)s; }

/* ─── Test helpers ───────────────────────────────────────────────────────── */

static void reset_tx(void)
{
    memset(s_tx_buf, 0, sizeof(s_tx_buf));
    s_tx_len = 0;
}

static const char *tx_str(void)
{
    s_tx_buf[s_tx_len] = '\0';
    return s_tx_buf;
}

void setUp(void)    { reset_tx(); }
void tearDown(void) {}

/* ─── Tests ──────────────────────────────────────────────────────────────── */

void test_send_ok_wire_format(void)
{
    uart_send_ok();
    TEST_ASSERT_EQUAL_STRING("S\n", tx_str());
}

void test_send_ok_with_wire_format(void)
{
    uart_send_ok_with("msg=\"PONG\"");
    TEST_ASSERT_EQUAL_STRING("S msg=\"PONG\"\n", tx_str());
}

void test_send_ok_with_multiple_fields(void)
{
    uart_send_ok_with("ip=\"192.168.1.42\" mac=\"AA:BB:CC:DD:EE:FF\"");
    TEST_ASSERT_EQUAL_STRING("S ip=\"192.168.1.42\" mac=\"AA:BB:CC:DD:EE:FF\"\n",
                             tx_str());
}

void test_send_stream_start_wire_format(void)
{
    uart_send_stream_start();
    TEST_ASSERT_EQUAL_STRING("S STREAM\n", tx_str());
}

void test_send_data_wire_format(void)
{
    uart_send_data("channel=\"6\" count=\"42\"");
    TEST_ASSERT_EQUAL_STRING("DATA channel=\"6\" count=\"42\"\n", tx_str());
}

void test_send_end_wire_format(void)
{
    uart_send_end();
    TEST_ASSERT_EQUAL_STRING("END\n", tx_str());
}

void test_send_stopped_wire_format(void)
{
    uart_send_stopped();
    TEST_ASSERT_EQUAL_STRING("STOPPED\n", tx_str());
}

void test_send_error_wire_format(void)
{
    uart_send_error("ERR_WRONG_MODE");
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_WRONG_MODE\"\n", tx_str());
}

void test_send_error_unknown_cmd(void)
{
    uart_send_error("ERR_UNKNOWN_CMD");
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_UNKNOWN_CMD\"\n", tx_str());
}

void test_send_raw_line_unchanged(void)
{
    uart_send("CUSTOM_LINE\n");
    TEST_ASSERT_EQUAL_STRING("CUSTOM_LINE\n", tx_str());
}

void test_full_stream_sequence(void)
{
    /* Checks that a complete stream produces the right concatenated output */
    uart_send_stream_start();
    uart_send_data("channel=\"1\" count=\"4\"");
    uart_send_data("channel=\"6\" count=\"18\"");
    uart_send_end();
    TEST_ASSERT_EQUAL_STRING(
        "S STREAM\n"
        "DATA channel=\"1\" count=\"4\"\n"
        "DATA channel=\"6\" count=\"18\"\n"
        "END\n",
        tx_str());
}

void test_stream_stopped_sequence(void)
{
    uart_send_stream_start();
    uart_send_data("rssi=\"-60\"");
    uart_send_stopped();
    TEST_ASSERT_EQUAL_STRING(
        "S STREAM\n"
        "DATA rssi=\"-60\"\n"
        "STOPPED\n",
        tx_str());
}

void test_stream_error_sequence(void)
{
    uart_send_stream_start();
    uart_send_data("ssid=\"Net\" rssi=\"-70\" open=\"true\"");
    uart_send_error("ERR_WIFI_TIMEOUT");
    TEST_ASSERT_EQUAL_STRING(
        "S STREAM\n"
        "DATA ssid=\"Net\" rssi=\"-70\" open=\"true\"\n"
        "E code=\"ERR_WIFI_TIMEOUT\"\n",
        tx_str());
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_send_ok_wire_format);
    RUN_TEST(test_send_ok_with_wire_format);
    RUN_TEST(test_send_ok_with_multiple_fields);
    RUN_TEST(test_send_stream_start_wire_format);
    RUN_TEST(test_send_data_wire_format);
    RUN_TEST(test_send_end_wire_format);
    RUN_TEST(test_send_stopped_wire_format);
    RUN_TEST(test_send_error_wire_format);
    RUN_TEST(test_send_error_unknown_cmd);
    RUN_TEST(test_send_raw_line_unchanged);
    RUN_TEST(test_full_stream_sequence);
    RUN_TEST(test_stream_stopped_sequence);
    RUN_TEST(test_stream_error_sequence);

    return UNITY_END();
}
