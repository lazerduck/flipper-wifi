#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include "router.h"
#include "parser.h"
#include "uart.h"
#include "sd_card.h"
#include "settings.h"
#include "led.h"
#include "wifi/wifi.h"
#include "ble/ble.h"
#include "zigbee/zigbee.h"
#include "esp_system.h"

void router_dispatch(const parsed_cmd_t *cmd, app_state_t *state)
{
    if (cmd->token_count == 0) {
        uart_send_error("ERR_UNKNOWN_CMD");
        return;
    }

    const char *t0 = cmd->tokens[0];

    /* ── PING ──────────────────────────────────────────────────────────────── */
    if (strcmp(t0, "PING") == 0) {
        uart_send_ok_with("msg=\"PONG\"");
        return;
    }

    /* ── SYS_* ─────────────────────────────────────────────────────────────── */
    if (strcmp(t0, "SYS") == 0) {
        if (cmd->token_count < 2) {
            uart_send_error("ERR_UNKNOWN_CMD");
            return;
        }
        const char *t1 = cmd->tokens[1];

        /* SYS_STOP — silent if nothing running; the running task sends STOPPED */
        if (strcmp(t1, "STOP") == 0) {
            if (state->active_op != NULL) {
                state->active_op->stop_requested = true;
            }
            return;
        }

        /* SYS_RESTART — no response; device reboots */
        if (strcmp(t1, "RESTART") == 0) {
            esp_restart();
            return;
        }

        if (cmd->token_count < 3) {
            uart_send_error("ERR_UNKNOWN_CMD");
            return;
        }
        const char *t2 = cmd->tokens[2];

        /* SYS_MODE_GET */
        if (strcmp(t1, "MODE") == 0 && strcmp(t2, "GET") == 0) {
            const char *mode_str;
            switch (state->mode) {
                case APP_MODE_WIFI:   mode_str = "WIFI";   break;
                case APP_MODE_BLE:    mode_str = "BLE";    break;
                case APP_MODE_ZIGBEE: mode_str = "ZIGBEE"; break;
                default:              mode_str = "IDLE";   break;
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "mode=\"%s\"", mode_str);
            uart_send_ok_with(buf);
            return;
        }

        /* SYS_MODE_SET */
        if (strcmp(t1, "MODE") == 0 && strcmp(t2, "SET") == 0) {
            const char *mode = parser_get_arg(cmd, "mode");
            if (!mode) { uart_send_error("ERR_MISSING_ARG"); return; }
            if (strcmp(mode, "WIFI") == 0) {
                wifi_init(state);
                state->mode = APP_MODE_WIFI;
                led_set_mode(APP_MODE_WIFI);
                uart_send_ok();
            } else if (strcmp(mode, "BLE") == 0) {
                ble_init(state);
                state->mode = APP_MODE_BLE;
                led_set_mode(APP_MODE_BLE);
                uart_send_ok();
            } else if (strcmp(mode, "ZIGBEE") == 0) {
                zigbee_init(state);
                state->mode = APP_MODE_ZIGBEE;
                led_set_mode(APP_MODE_ZIGBEE);
                uart_send_ok();
            } else {
                uart_send_error("ERR_MODE_INIT_FAIL");
            }
            return;
        }

        /* SYS_TIME_SET */
        if (strcmp(t1, "TIME") == 0 && strcmp(t2, "SET") == 0) {
            const char *epoch = parser_get_arg(cmd, "epoch");
            if (!epoch) { uart_send_error("ERR_MISSING_ARG"); return; }
            struct timeval tv = { .tv_sec = (time_t)atol(epoch), .tv_usec = 0 };
            settimeofday(&tv, NULL);
            uart_send_ok();
            return;
        }

        uart_send_error("ERR_UNKNOWN_CMD");
        return;
    }

    /* ── LED_* ─────────────────────────────────────────────────────────────── */
    if (strcmp(t0, "LED") == 0) {
        led_handle_command(cmd);
        return;
    }

    /* ── Radio commands — require the matching mode ────────────────────────── */
    if (strcmp(t0, "WIFI") == 0) {
        if (state->mode != APP_MODE_WIFI) { uart_send_error("ERR_WRONG_MODE"); return; }
        wifi_handle_command(cmd, 1, state);
        return;
    }

    if (strcmp(t0, "BLE") == 0) {
        if (state->mode != APP_MODE_BLE) { uart_send_error("ERR_WRONG_MODE"); return; }
        ble_handle_command(cmd, 1, state);
        return;
    }

    if (strcmp(t0, "ZIGBEE") == 0) {
        if (state->mode != APP_MODE_ZIGBEE) { uart_send_error("ERR_WRONG_MODE"); return; }
        zigbee_handle_command(cmd, 1, state);
        return;
    }

    /* ── SD / Settings — no mode restriction ──────────────────────────────── */
    if (strcmp(t0, "SD") == 0) {
        sd_handle_command(cmd, &state->sd);
        return;
    }

    if (strcmp(t0, "SETTINGS") == 0) {
        settings_handle_command(cmd, &state->sd);
        return;
    }

    uart_send_error("ERR_UNKNOWN_CMD");
}
