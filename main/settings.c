#include "settings.h"
#include "sd_card.h"
#include "uart.h"
#include "parser.h"

#include <stdio.h>
#include <string.h>

/* ── Storage ─────────────────────────────────────────────────────────────── */

#define SETTINGS_MAX     32
#define SETTINGS_KEY_MAX 32
#define SETTINGS_VAL_MAX 128
#define SETTINGS_FILE    SD_ROOT "/settings.txt"

/* Line buffer: key(32) + '='(1) + value(128) + '\n'(1) + null(1) = 163 */
#define SETTINGS_LINE_MAX (SETTINGS_KEY_MAX + 1 + SETTINGS_VAL_MAX + 2)

typedef struct {
    char key[SETTINGS_KEY_MAX + 1];
    char val[SETTINGS_VAL_MAX + 1];
} setting_t;

static setting_t s_store[SETTINGS_MAX];
static int       s_count;

/* ── Persistence helpers ─────────────────────────────────────────────────── */

/* Write the full in-memory table back to disk. */
static bool persist(void)
{
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) return false;
    for (int i = 0; i < s_count; i++) {
        if (fprintf(f, "%s=%s\n", s_store[i].key, s_store[i].val) < 0) {
            fclose(f);
            return false;
        }
    }
    fclose(f);
    return true;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void settings_init(sd_state_t *sd)
{
    s_count = 0;
    if (!sd->mounted) return;

    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) return;

    char line[SETTINGS_LINE_MAX];
    while (fgets(line, sizeof(line), f) && s_count < SETTINGS_MAX) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *k = line;
        const char *v = eq + 1;

        size_t klen = strlen(k);
        if (klen == 0 || klen > SETTINGS_KEY_MAX) continue;
        if (strlen(v) > SETTINGS_VAL_MAX) continue;

        strncpy(s_store[s_count].key, k, SETTINGS_KEY_MAX);
        s_store[s_count].key[SETTINGS_KEY_MAX] = '\0';
        strncpy(s_store[s_count].val, v, SETTINGS_VAL_MAX);
        s_store[s_count].val[SETTINGS_VAL_MAX] = '\0';
        s_count++;
    }
    fclose(f);
}

const char *settings_get(const char *key, const char *default_value)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_store[i].key, key) == 0) return s_store[i].val;
    }
    return default_value;
}

bool settings_set(const char *key, const char *value, sd_state_t *sd)
{
    if (!sd->mounted) return false;

    size_t klen = strlen(key);
    if (klen == 0 || klen > SETTINGS_KEY_MAX) return false;
    if (strlen(value) > SETTINGS_VAL_MAX) return false;

    /* Update existing entry if the key is already present. */
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_store[i].key, key) == 0) {
            strncpy(s_store[i].val, value, SETTINGS_VAL_MAX);
            s_store[i].val[SETTINGS_VAL_MAX] = '\0';
            return persist();
        }
    }

    /* Insert new entry. */
    if (s_count >= SETTINGS_MAX) return false;
    strncpy(s_store[s_count].key, key, SETTINGS_KEY_MAX);
    s_store[s_count].key[SETTINGS_KEY_MAX] = '\0';
    strncpy(s_store[s_count].val, value, SETTINGS_VAL_MAX);
    s_store[s_count].val[SETTINGS_VAL_MAX] = '\0';
    s_count++;
    return persist();
}

void settings_handle_command(const parsed_cmd_t *cmd, sd_state_t *sd)
{
    if (cmd->token_count < 2) { uart_send_error("ERR_UNKNOWN_CMD"); return; }
    const char *sub = cmd->tokens[1];

    if (strcmp(sub, "GET") == 0) {
        const char *key = parser_get_arg(cmd, "key");
        if (!key) { uart_send_error("ERR_MISSING_ARG"); return; }
        const char *val = settings_get(key, NULL);
        if (!val) { uart_send_error("ERR_SETTINGS_KEY_NOT_FOUND"); return; }
        char buf[SETTINGS_VAL_MAX + 16];
        snprintf(buf, sizeof(buf), "value=\"%s\"", val);
        uart_send_ok_with(buf);

    } else if (strcmp(sub, "SET") == 0) {
        const char *key = parser_get_arg(cmd, "key");
        const char *val = parser_get_arg(cmd, "value");
        if (!key || !val) { uart_send_error("ERR_MISSING_ARG"); return; }
        if (!sd->mounted) { uart_send_error("ERR_SD_NOT_MOUNTED"); return; }
        settings_set(key, val, sd) ? uart_send_ok() : uart_send_error("ERR_SD_WRITE_FAIL");

    } else {
        uart_send_error("ERR_UNKNOWN_CMD");
    }
}
