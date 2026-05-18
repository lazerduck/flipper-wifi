#include "unity.h"
#include "settings.h"
#include "sd_card.h"
#include "parser.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ─── ESP-IDF UART driver stubs ──────────────────────────────────────────── */
#include "driver/uart.h"
#include "freertos/task.h"

static char s_tx_buf[2048];
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

esp_err_t uart_param_config(uart_port_t p, const uart_config_t *c)
{ (void)p; (void)c; return 0; }

esp_err_t uart_set_pin(uart_port_t p, int tx, int rx, int rts, int cts)
{ (void)p; (void)tx; (void)rx; (void)rts; (void)cts; return 0; }

int uart_read_bytes(uart_port_t p, void *buf, uint32_t len, TickType_t ticks)
{ (void)p; (void)buf; (void)len; (void)ticks; return 0; }

BaseType_t xTaskCreate(void (*fn)(void *), const char *name,
                       uint32_t stack, void *param,
                       UBaseType_t prio, TaskHandle_t *hdl)
{ (void)fn; (void)name; (void)stack; (void)param; (void)prio; (void)hdl;
  return pdTRUE; }

/* ─── parser / router stubs ──────────────────────────────────────────────── *
 * parser_parse and parser_free come from the linked parser.c.               */
#include "router.h"
void router_dispatch(const parsed_cmd_t *c, app_state_t *s) { (void)c; (void)s; }

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

static parsed_cmd_t make_settings_cmd(char *subcmd)
{
    parsed_cmd_t cmd = {0};
    cmd.tokens[0]   = "SETTINGS";
    cmd.tokens[1]   = subcmd;
    cmd.token_count = 2;
    return cmd;
}

static void add_arg(parsed_cmd_t *cmd, char *key, char *value)
{
    int i = cmd->arg_count++;
    cmd->args[i].key   = key;
    cmd->args[i].value = value;
}

/* Write a raw settings file into the test SD root. */
static void write_settings_file(const char *content)
{
    FILE *f = fopen(SD_ROOT "/settings.txt", "w");
    if (f) { fputs(content, f); fclose(f); }
}

void setUp(void)
{
    system("rm -rf " SD_ROOT " && mkdir -p " SD_ROOT);
    reset_tx();
    /* Reset in-memory table by initialising with card absent. */
    sd_state_t none = { .present = false, .mounted = false };
    settings_init(&none);
}

void tearDown(void)
{
    system("rm -rf " SD_ROOT);
}

/* ─── settings_get / settings_set ───────────────────────────────────────── */

void test_get_returns_default_when_table_empty(void)
{
    TEST_ASSERT_EQUAL_STRING("default", settings_get("ssid", "default"));
}

void test_get_returns_null_default_when_not_found(void)
{
    TEST_ASSERT_NULL(settings_get("missing", NULL));
}

void test_set_and_get_roundtrip(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    TEST_ASSERT_TRUE(settings_set("ssid", "MyNetwork", &sd));
    TEST_ASSERT_EQUAL_STRING("MyNetwork", settings_get("ssid", NULL));
}

void test_set_updates_existing_key(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    settings_set("ssid", "OldNet", &sd);
    settings_set("ssid", "NewNet", &sd);
    TEST_ASSERT_EQUAL_STRING("NewNet", settings_get("ssid", NULL));
}

void test_set_multiple_independent_keys(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    settings_set("ssid",     "MyNet", &sd);
    settings_set("password", "secret", &sd);
    settings_set("channel",  "6",     &sd);
    TEST_ASSERT_EQUAL_STRING("MyNet",  settings_get("ssid",     NULL));
    TEST_ASSERT_EQUAL_STRING("secret", settings_get("password", NULL));
    TEST_ASSERT_EQUAL_STRING("6",      settings_get("channel",  NULL));
}

void test_set_fails_when_not_mounted(void)
{
    sd_state_t sd = { .present = false, .mounted = false };
    TEST_ASSERT_FALSE(settings_set("ssid", "x", &sd));
    TEST_ASSERT_NULL(settings_get("ssid", NULL));
}

/* ─── Persistence ────────────────────────────────────────────────────────── */

void test_set_persists_key_value_to_file(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    settings_set("ssid", "PersistNet", &sd);

    char buf[256];
    FILE *f = fopen(SD_ROOT "/settings.txt", "r");
    TEST_ASSERT_NOT_NULL(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    TEST_ASSERT_NOT_NULL(strstr(buf, "ssid=PersistNet"));
}

void test_set_update_writes_single_entry(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    settings_set("key", "v1", &sd);
    settings_set("key", "v2", &sd);

    /* File should have exactly one entry, not two. */
    char buf[256];
    FILE *f = fopen(SD_ROOT "/settings.txt", "r");
    TEST_ASSERT_NOT_NULL(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    TEST_ASSERT_EQUAL_STRING("key=v2\n", buf);
}

void test_init_loads_from_file(void)
{
    write_settings_file("ssid=LoadedNet\npassword=abc123\n");
    sd_state_t sd = { .present = true, .mounted = true };
    settings_init(&sd);

    TEST_ASSERT_EQUAL_STRING("LoadedNet", settings_get("ssid",     NULL));
    TEST_ASSERT_EQUAL_STRING("abc123",    settings_get("password", NULL));
}

void test_init_ignores_malformed_lines(void)
{
    write_settings_file("good=value\nbadline\n=nokey\n");
    sd_state_t sd = { .present = true, .mounted = true };
    settings_init(&sd);

    TEST_ASSERT_EQUAL_STRING("value", settings_get("good", NULL));
    TEST_ASSERT_NULL(settings_get("badline", NULL));
    TEST_ASSERT_NULL(settings_get("", NULL));
}

void test_init_noop_when_not_mounted(void)
{
    write_settings_file("ssid=ShouldNotLoad\n");
    sd_state_t sd = { .present = false, .mounted = false };
    settings_init(&sd);

    TEST_ASSERT_NULL(settings_get("ssid", NULL));
}

void test_init_no_file_does_not_crash(void)
{
    /* No settings.txt written — init should succeed silently. */
    sd_state_t sd = { .present = true, .mounted = true };
    settings_init(&sd);
    TEST_ASSERT_NULL(settings_get("anything", NULL));
}

void test_value_with_equals_sign_preserved(void)
{
    /* Values that contain '=' should round-trip correctly. */
    write_settings_file("token=abc=def=ghi\n");
    sd_state_t sd = { .present = true, .mounted = true };
    settings_init(&sd);
    TEST_ASSERT_EQUAL_STRING("abc=def=ghi", settings_get("token", NULL));
}

void test_survive_reinit_after_set(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    settings_set("x", "1", &sd);
    settings_set("y", "2", &sd);

    /* Re-initialise from the file that was just written. */
    settings_init(&sd);

    TEST_ASSERT_EQUAL_STRING("1", settings_get("x", NULL));
    TEST_ASSERT_EQUAL_STRING("2", settings_get("y", NULL));
}

/* ─── UART command handler ───────────────────────────────────────────────── */

void test_uart_get_returns_value(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    settings_set("ssid", "MyNet", &sd);
    reset_tx();

    parsed_cmd_t cmd = make_settings_cmd("GET");
    add_arg(&cmd, "key", "ssid");
    settings_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("S value=\"MyNet\"\n", tx_str());
}

void test_uart_get_key_not_found(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_settings_cmd("GET");
    add_arg(&cmd, "key", "nope");
    settings_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_SETTINGS_KEY_NOT_FOUND\"\n", tx_str());
}

void test_uart_get_missing_key_arg(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_settings_cmd("GET");
    settings_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_MISSING_ARG\"\n", tx_str());
}

void test_uart_set_persists_and_replies_ok(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_settings_cmd("SET");
    add_arg(&cmd, "key",   "channel");
    add_arg(&cmd, "value", "11");
    settings_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("S\n", tx_str());
    TEST_ASSERT_EQUAL_STRING("11", settings_get("channel", NULL));
}

void test_uart_set_missing_arg(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_settings_cmd("SET");
    add_arg(&cmd, "key", "ssid");
    /* value arg absent */
    settings_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_MISSING_ARG\"\n", tx_str());
}

void test_uart_set_not_mounted(void)
{
    sd_state_t sd = { .present = false, .mounted = false };
    parsed_cmd_t cmd = make_settings_cmd("SET");
    add_arg(&cmd, "key",   "ssid");
    add_arg(&cmd, "value", "x");
    settings_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_SD_NOT_MOUNTED\"\n", tx_str());
}

void test_uart_unknown_subcommand(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_settings_cmd("DELETE");
    settings_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_UNKNOWN_CMD\"\n", tx_str());
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_get_returns_default_when_table_empty);
    RUN_TEST(test_get_returns_null_default_when_not_found);
    RUN_TEST(test_set_and_get_roundtrip);
    RUN_TEST(test_set_updates_existing_key);
    RUN_TEST(test_set_multiple_independent_keys);
    RUN_TEST(test_set_fails_when_not_mounted);

    RUN_TEST(test_set_persists_key_value_to_file);
    RUN_TEST(test_set_update_writes_single_entry);
    RUN_TEST(test_init_loads_from_file);
    RUN_TEST(test_init_ignores_malformed_lines);
    RUN_TEST(test_init_noop_when_not_mounted);
    RUN_TEST(test_init_no_file_does_not_crash);
    RUN_TEST(test_value_with_equals_sign_preserved);
    RUN_TEST(test_survive_reinit_after_set);

    RUN_TEST(test_uart_get_returns_value);
    RUN_TEST(test_uart_get_key_not_found);
    RUN_TEST(test_uart_get_missing_key_arg);
    RUN_TEST(test_uart_set_persists_and_replies_ok);
    RUN_TEST(test_uart_set_missing_arg);
    RUN_TEST(test_uart_set_not_mounted);
    RUN_TEST(test_uart_unknown_subcommand);

    return UNITY_END();
}
