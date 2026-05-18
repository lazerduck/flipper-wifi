#include "unity.h"
#include "sd_card.h"
#include "parser.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>

/* ─── sd_init stub (guarded in sd_card.c with UNIT_TEST) ─────────────────── */
void sd_init(sd_state_t *sd) { (void)sd; }

/* ─── ESP-IDF UART driver stubs ──────────────────────────────────────────── *
 * uart.c is compiled into this target; we stub the hardware calls so it      *
 * links, and mock uart_write_bytes so tests can assert on wire output.       */
#include "driver/uart.h"
#include "freertos/task.h"

static char s_tx_buf[4096]; /* larger: SD streams can be multi-line */
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

/* ─── parser / router stubs (called by uart_reader_task, not under test) ─── *
 * parser_parse and parser_free come from the linked parser.c.               */
#include "router.h"
void router_dispatch(const parsed_cmd_t *c, app_state_t *s) { (void)c; (void)s; }

/* ─── Test helpers ───────────────────────────────────────────────────────── */

#define SD_TEST_ROOT SD_ROOT

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

static parsed_cmd_t make_sd_cmd(char *subcmd)
{
    parsed_cmd_t cmd = {0};
    cmd.tokens[0]   = "SD";
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

/* Write a file directly into the test SD root (bypasses sd_handle_command). */
static void write_test_file(const char *rel_path, const char *content)
{
    char full[256];
    snprintf(full, sizeof(full), "%s%s", SD_TEST_ROOT, rel_path);
    FILE *f = fopen(full, "w");
    if (f) { fputs(content, f); fclose(f); }
}

void setUp(void)
{
    system("rm -rf " SD_TEST_ROOT " && mkdir -p " SD_TEST_ROOT);
    reset_tx();
}

void tearDown(void)
{
    system("rm -rf " SD_TEST_ROOT);
}

/* ─── sd_handle_command — STATUS ─────────────────────────────────────────── */

void test_status_reports_not_mounted(void)
{
    sd_state_t sd = { .present = false, .mounted = false };
    parsed_cmd_t cmd = make_sd_cmd("STATUS");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_NOT_NULL(strstr(tx_str(), "present=\"false\""));
    TEST_ASSERT_NOT_NULL(strstr(tx_str(), "mounted=\"false\""));
}

void test_status_reports_mounted(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("STATUS");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_NOT_NULL(strstr(tx_str(), "present=\"true\""));
    TEST_ASSERT_NOT_NULL(strstr(tx_str(), "mounted=\"true\""));
}

/* ─── sd_handle_command — not-mounted guard ──────────────────────────────── */

void test_not_mounted_returns_error(void)
{
    sd_state_t sd = { .present = false, .mounted = false };
    const char *cmds[] = { "LIST", "READ", "WRITE", "APPEND", "MKDIR", "DELETE", "FORMAT" };
    for (int i = 0; i < (int)(sizeof(cmds) / sizeof(cmds[0])); i++) {
        reset_tx();
        parsed_cmd_t cmd = make_sd_cmd((char *)cmds[i]);
        add_arg(&cmd, "path", "/x");
        add_arg(&cmd, "data", "x");
        sd_handle_command(&cmd, &sd);
        TEST_ASSERT_EQUAL_STRING("E code=\"ERR_SD_NOT_MOUNTED\"\n", tx_str());
    }
}

/* ─── sd_handle_command — LIST ───────────────────────────────────────────── */

void test_list_empty_dir_streams_end(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("LIST");
    add_arg(&cmd, "path", "/");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("S STREAM\nEND\n", tx_str());
}

void test_list_shows_file_entry(void)
{
    write_test_file("/hello.txt", "hi");
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("LIST");
    add_arg(&cmd, "path", "/");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_NOT_NULL(strstr(tx_str(), "name=\"hello.txt\""));
    TEST_ASSERT_NOT_NULL(strstr(tx_str(), "dir=\"false\""));
    TEST_ASSERT_NOT_NULL(strstr(tx_str(), "END\n"));
}

void test_list_shows_directory_entry(void)
{
    char dir[128];
    snprintf(dir, sizeof(dir), "%s/logs", SD_TEST_ROOT);
    mkdir(dir, 0755);

    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("LIST");
    add_arg(&cmd, "path", "/");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_NOT_NULL(strstr(tx_str(), "name=\"logs\""));
    TEST_ASSERT_NOT_NULL(strstr(tx_str(), "dir=\"true\""));
}

void test_list_path_not_found(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("LIST");
    add_arg(&cmd, "path", "/nosuchdir");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_SD_PATH_NOT_FOUND\"\n", tx_str());
}

/* ─── sd_handle_command — READ ───────────────────────────────────────────── */

void test_read_streams_lines(void)
{
    write_test_file("/msg.txt", "hello\nworld\n");
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("READ");
    add_arg(&cmd, "path", "/msg.txt");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING(
        "S STREAM\n"
        "DATA line=\"hello\"\n"
        "DATA line=\"world\"\n"
        "END\n",
        tx_str());
}

void test_read_single_line_no_trailing_newline(void)
{
    write_test_file("/one.txt", "just one line");
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("READ");
    add_arg(&cmd, "path", "/one.txt");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING(
        "S STREAM\n"
        "DATA line=\"just one line\"\n"
        "END\n",
        tx_str());
}

void test_read_file_not_found(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("READ");
    add_arg(&cmd, "path", "/nope.txt");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_SD_PATH_NOT_FOUND\"\n", tx_str());
}

/* ─── sd_handle_command — WRITE ──────────────────────────────────────────── */

void test_write_creates_file(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("WRITE");
    add_arg(&cmd, "path", "/out.txt");
    add_arg(&cmd, "data", "saved content");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("S\n", tx_str());

    char buf[64];
    TEST_ASSERT_TRUE(sd_read_file(SD_TEST_ROOT "/out.txt", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("saved content", buf);
}

void test_write_overwrites_existing(void)
{
    write_test_file("/over.txt", "old");
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("WRITE");
    add_arg(&cmd, "path", "/over.txt");
    add_arg(&cmd, "data", "new");
    sd_handle_command(&cmd, &sd);

    char buf[32];
    sd_read_file(SD_TEST_ROOT "/over.txt", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("new", buf);
}

void test_write_missing_arg(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("WRITE");
    add_arg(&cmd, "path", "/x.txt");
    /* no data arg */
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_MISSING_ARG\"\n", tx_str());
}

/* ─── sd_handle_command — APPEND ─────────────────────────────────────────── */

void test_append_accumulates_content(void)
{
    sd_state_t sd = { .present = true, .mounted = true };

    parsed_cmd_t cmd1 = make_sd_cmd("WRITE");
    add_arg(&cmd1, "path", "/log.txt");
    add_arg(&cmd1, "data", "line1");
    sd_handle_command(&cmd1, &sd);

    reset_tx();
    parsed_cmd_t cmd2 = make_sd_cmd("APPEND");
    add_arg(&cmd2, "path", "/log.txt");
    add_arg(&cmd2, "data", "line2");
    sd_handle_command(&cmd2, &sd);
    TEST_ASSERT_EQUAL_STRING("S\n", tx_str());

    char buf[64];
    sd_read_file(SD_TEST_ROOT "/log.txt", buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "line1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "line2"));
}

/* ─── sd_handle_command — MKDIR ──────────────────────────────────────────── */

void test_mkdir_creates_directory(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("MKDIR");
    add_arg(&cmd, "path", "/newdir");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("S\n", tx_str());

    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(SD_TEST_ROOT "/newdir", &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
}

void test_mkdir_missing_arg(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("MKDIR");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_MISSING_ARG\"\n", tx_str());
}

/* ─── sd_handle_command — DELETE ─────────────────────────────────────────── */

void test_delete_removes_file(void)
{
    write_test_file("/del.txt", "bye");
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("DELETE");
    add_arg(&cmd, "path", "/del.txt");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("S\n", tx_str());

    struct stat st;
    TEST_ASSERT_NOT_EQUAL(0, stat(SD_TEST_ROOT "/del.txt", &st));
}

void test_delete_removes_empty_dir(void)
{
    char dir[128];
    snprintf(dir, sizeof(dir), "%s/emptydir", SD_TEST_ROOT);
    mkdir(dir, 0755);

    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("DELETE");
    add_arg(&cmd, "path", "/emptydir");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("S\n", tx_str());
}

void test_delete_nonexistent_returns_error(void)
{
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("DELETE");
    add_arg(&cmd, "path", "/ghost.txt");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_SD_PATH_NOT_FOUND\"\n", tx_str());
}

void test_delete_nonempty_dir_returns_error(void)
{
    char dir[128];
    snprintf(dir, sizeof(dir), "%s/full", SD_TEST_ROOT);
    mkdir(dir, 0755);
    char f[160];
    snprintf(f, sizeof(f), "%s/x.txt", dir);
    FILE *fp = fopen(f, "w"); fputs("x", fp); fclose(fp);

    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("DELETE");
    add_arg(&cmd, "path", "/full");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("E code=\"ERR_SD_NOT_EMPTY\"\n", tx_str());
}

/* ─── sd_handle_command — FORMAT ─────────────────────────────────────────── */

void test_format_succeeds_when_mounted(void)
{
    /* In UNIT_TEST mode sd_cmd_format() just returns S\n */
    sd_state_t sd = { .present = true, .mounted = true };
    parsed_cmd_t cmd = make_sd_cmd("FORMAT");
    sd_handle_command(&cmd, &sd);
    TEST_ASSERT_EQUAL_STRING("S\n", tx_str());
}

/* ─── Low-level helpers ──────────────────────────────────────────────────── */

void test_sd_read_write_helpers(void)
{
    TEST_ASSERT_TRUE(sd_write_file(SD_TEST_ROOT "/rw.txt", "abc"));
    char buf[16];
    TEST_ASSERT_TRUE(sd_read_file(SD_TEST_ROOT "/rw.txt", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("abc", buf);
}

void test_sd_append_helper(void)
{
    sd_write_file(SD_TEST_ROOT "/app.txt", "A");
    sd_append_file(SD_TEST_ROOT "/app.txt", "B");
    char buf[16];
    sd_read_file(SD_TEST_ROOT "/app.txt", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("AB", buf);
}

void test_sd_delete_helper(void)
{
    sd_write_file(SD_TEST_ROOT "/gone.txt", "x");
    TEST_ASSERT_TRUE(sd_delete_file(SD_TEST_ROOT "/gone.txt"));
    TEST_ASSERT_FALSE(sd_delete_file(SD_TEST_ROOT "/gone.txt")); /* already gone */
}

void test_sd_mkdir_helper(void)
{
    TEST_ASSERT_TRUE(sd_mkdir(SD_TEST_ROOT "/sub"));
    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(SD_TEST_ROOT "/sub", &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_status_reports_not_mounted);
    RUN_TEST(test_status_reports_mounted);
    RUN_TEST(test_not_mounted_returns_error);

    RUN_TEST(test_list_empty_dir_streams_end);
    RUN_TEST(test_list_shows_file_entry);
    RUN_TEST(test_list_shows_directory_entry);
    RUN_TEST(test_list_path_not_found);

    RUN_TEST(test_read_streams_lines);
    RUN_TEST(test_read_single_line_no_trailing_newline);
    RUN_TEST(test_read_file_not_found);

    RUN_TEST(test_write_creates_file);
    RUN_TEST(test_write_overwrites_existing);
    RUN_TEST(test_write_missing_arg);

    RUN_TEST(test_append_accumulates_content);

    RUN_TEST(test_mkdir_creates_directory);
    RUN_TEST(test_mkdir_missing_arg);

    RUN_TEST(test_delete_removes_file);
    RUN_TEST(test_delete_removes_empty_dir);
    RUN_TEST(test_delete_nonexistent_returns_error);
    RUN_TEST(test_delete_nonempty_dir_returns_error);

    RUN_TEST(test_format_succeeds_when_mounted);

    RUN_TEST(test_sd_read_write_helpers);
    RUN_TEST(test_sd_append_helper);
    RUN_TEST(test_sd_delete_helper);
    RUN_TEST(test_sd_mkdir_helper);

    return UNITY_END();
}
