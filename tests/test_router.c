#include "unity.h"
#include "router.h"
#include "parser.h"
#include "uart.h"
#include "led.h"
#include "sd_card.h"
#include "settings.h"
#include "wifi/wifi.h"
#include "ble/ble.h"
#include "zigbee/zigbee.h"

/* ─── Mocks ───────────────────────────────────────────────────────────────── *
 * These provide definitions for every external function that router.c will    *
 * call once implemented.  They are unused while router_dispatch() is a stub,  *
 * but are already here so the file needs no structural changes when the router *
 * is filled in.                                                                *
 *                                                                             *
 * Each mock records what was called so tests can assert routing decisions.    */

static const char *s_last_error_code;
static const char *s_last_module_called;
static bool        s_ok_sent;

/* esp_system stub */
#include "esp_system.h"
void esp_restart(void) {}

/* uart mocks */
void uart_init(app_state_t *s)          { (void)s; }
void uart_send(const char *l)           { (void)l; }
void uart_send_ok(void)                 { s_ok_sent = true; }
void uart_send_ok_with(const char *f)   { (void)f; s_ok_sent = true; }
void uart_send_stream_start(void)       {}
void uart_send_data(const char *f)      { (void)f; }
void uart_send_end(void)                {}
void uart_send_stopped(void)            {}
void uart_send_error(const char *code)  { s_last_error_code = code; }

/* led mocks */
void led_init(void)                             {}
void led_set(uint8_t r, uint8_t g, uint8_t b)  { (void)r; (void)g; (void)b; }
void led_set_mode(app_mode_t m)                 { (void)m; }
void led_pulse_boot(void)                       {}
void led_blink_start(void)                      {}
void led_blink_stop(void)                       {}

/* wifi mocks */
void wifi_init(app_state_t *s)    { (void)s; }
void wifi_deinit(app_state_t *s)  { (void)s; }
void wifi_handle_command(const parsed_cmd_t *c, int o, app_state_t *s)
    { (void)c; (void)o; (void)s; s_last_module_called = "WIFI"; }

/* ble mocks */
void ble_init(app_state_t *s)     { (void)s; }
void ble_deinit(app_state_t *s)   { (void)s; }
void ble_handle_command(const parsed_cmd_t *c, int o, app_state_t *s)
    { (void)c; (void)o; (void)s; s_last_module_called = "BLE"; }

/* zigbee mocks */
void zigbee_init(app_state_t *s)    { (void)s; }
void zigbee_deinit(app_state_t *s)  { (void)s; }
void zigbee_handle_command(const parsed_cmd_t *c, int o, app_state_t *s)
    { (void)c; (void)o; (void)s; s_last_module_called = "ZIGBEE"; }

/* sd / settings mocks */
void sd_init(sd_state_t *s)                               { (void)s; }
void sd_handle_command(const parsed_cmd_t *c, sd_state_t *s)
    { (void)c; (void)s; }
void settings_init(sd_state_t *s)                         { (void)s; }
void settings_handle_command(const parsed_cmd_t *c, sd_state_t *s)
    { (void)c; (void)s; }
const char *settings_get(const char *k, const char *def)  { (void)k; return def; }
bool        settings_set(const char *k, const char *v, sd_state_t *s)
    { (void)k; (void)v; (void)s; return false; }
bool sd_read_file(const char *p, char *b, size_t n)
    { (void)p; (void)b; (void)n; return false; }
bool sd_write_file(const char *p, const char *d)
    { (void)p; (void)d; return false; }
void sd_status(const sd_state_t *s) { (void)s; }

/* ─── Helpers ─────────────────────────────────────────────────────────────── *
 * Build a parsed_cmd_t by hand so router tests don't depend on parser_parse() *
 * being implemented.  Set token pointers directly into fixed string literals. */

static parsed_cmd_t make_cmd_1(char *t0)
{
    parsed_cmd_t cmd = {0};
    cmd.tokens[0]  = t0;
    cmd.token_count = 1;
    return cmd;
}

static parsed_cmd_t make_cmd_2(char *t0, char *t1)
{
    parsed_cmd_t cmd = make_cmd_1(t0);
    cmd.tokens[1]  = t1;
    cmd.token_count = 2;
    return cmd;
}

static parsed_cmd_t make_cmd_3(char *t0, char *t1, char *t2)
{
    parsed_cmd_t cmd = make_cmd_2(t0, t1);
    cmd.tokens[2]  = t2;
    cmd.token_count = 3;
    return cmd;
}

/* ─── Test setup ──────────────────────────────────────────────────────────── */

void setUp(void)
{
    s_last_error_code    = NULL;
    s_last_module_called = NULL;
    s_ok_sent            = false;
}

void tearDown(void) {}

/* ─── Tests that pass today ───────────────────────────────────────────────── */

void test_router_dispatch_does_not_crash(void)
{
    app_state_t  state = { .mode = APP_MODE_IDLE, .active_op = NULL };
    parsed_cmd_t cmd   = {0};
    router_dispatch(&cmd, &state);
    TEST_PASS_MESSAGE("completed without crash");
}

/* ─── Tests requiring a real router_dispatch() implementation ────────────── *
 * Remove TEST_IGNORE_MESSAGE and verify the assertion in the comment once     *
 * router_dispatch() is implemented in router.c.                               *
 *                                                                             *
 * The make_cmd_1/make_cmd_2 helpers above let you build a cmd without relying *
 * on parser_parse(), e.g.:                                                    *
 *   app_state_t  state = { .mode = APP_MODE_IDLE };                           *
 *   parsed_cmd_t cmd   = make_cmd_1("PING");                                  *
 *   router_dispatch(&cmd, &state);                                            *
 *   TEST_ASSERT_TRUE(s_ok_sent);                                              */

void test_ping_replies_with_pong(void)
{
    app_state_t  state = { .mode = APP_MODE_IDLE, .active_op = NULL };
    parsed_cmd_t cmd   = make_cmd_1("PING");
    router_dispatch(&cmd, &state);
    TEST_ASSERT_TRUE(s_ok_sent);
}

void test_unknown_command_sends_err_unknown_cmd(void)
{
    app_state_t  state = { .mode = APP_MODE_IDLE, .active_op = NULL };
    parsed_cmd_t cmd   = make_cmd_1("BLOOP");
    router_dispatch(&cmd, &state);
    TEST_ASSERT_EQUAL_STRING("ERR_UNKNOWN_CMD", s_last_error_code);
}

void test_wifi_command_forwarded_to_wifi_module(void)
{
    app_state_t  state = { .mode = APP_MODE_WIFI, .active_op = NULL };
    parsed_cmd_t cmd   = make_cmd_2("WIFI", "SCAN");
    router_dispatch(&cmd, &state);
    TEST_ASSERT_EQUAL_STRING("WIFI", s_last_module_called);
}

void test_ble_command_forwarded_to_ble_module(void)
{
    app_state_t  state = { .mode = APP_MODE_BLE, .active_op = NULL };
    parsed_cmd_t cmd   = make_cmd_2("BLE", "SCAN");
    router_dispatch(&cmd, &state);
    TEST_ASSERT_EQUAL_STRING("BLE", s_last_module_called);
}

void test_zigbee_command_forwarded_to_zigbee_module(void)
{
    app_state_t  state = { .mode = APP_MODE_ZIGBEE, .active_op = NULL };
    parsed_cmd_t cmd   = make_cmd_2("ZIGBEE", "CHANNEL_SCAN");
    router_dispatch(&cmd, &state);
    TEST_ASSERT_EQUAL_STRING("ZIGBEE", s_last_module_called);
}

void test_sys_stop_with_no_active_op_is_silent(void)
{
    app_state_t  state = { .mode = APP_MODE_IDLE, .active_op = NULL };
    parsed_cmd_t cmd   = make_cmd_2("SYS", "STOP");
    router_dispatch(&cmd, &state);
    TEST_ASSERT_FALSE(s_ok_sent);
    TEST_ASSERT_NULL(s_last_error_code);
}

void test_sys_mode_set_wifi_calls_wifi_init(void)
{
    app_state_t  state = { .mode = APP_MODE_IDLE, .active_op = NULL };
    parsed_cmd_t cmd   = make_cmd_3("SYS", "MODE", "SET");
    cmd.args[0].key    = "mode";
    cmd.args[0].value  = "WIFI";
    cmd.arg_count      = 1;
    router_dispatch(&cmd, &state);
    TEST_ASSERT_EQUAL_INT(APP_MODE_WIFI, state.mode);
    TEST_ASSERT_TRUE(s_ok_sent);
}

void test_command_rejected_in_wrong_mode(void)
{
    app_state_t  state = { .mode = APP_MODE_BLE, .active_op = NULL };
    parsed_cmd_t cmd   = make_cmd_2("WIFI", "SCAN");
    router_dispatch(&cmd, &state);
    TEST_ASSERT_EQUAL_STRING("ERR_WRONG_MODE", s_last_error_code);
}

int main(void)
{
    UNITY_BEGIN();

    /* Passing today */
    RUN_TEST(test_router_dispatch_does_not_crash);

    /* Ignored until router_dispatch() is implemented */
    RUN_TEST(test_ping_replies_with_pong);
    RUN_TEST(test_unknown_command_sends_err_unknown_cmd);
    RUN_TEST(test_wifi_command_forwarded_to_wifi_module);
    RUN_TEST(test_ble_command_forwarded_to_ble_module);
    RUN_TEST(test_zigbee_command_forwarded_to_zigbee_module);
    RUN_TEST(test_sys_stop_with_no_active_op_is_silent);
    RUN_TEST(test_sys_mode_set_wifi_calls_wifi_init);
    RUN_TEST(test_command_rejected_in_wrong_mode);

    return UNITY_END();
}
