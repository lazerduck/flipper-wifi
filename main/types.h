#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ─── Application mode ───────────────────────────────────────────────────── */

typedef enum {
    APP_MODE_IDLE,
    APP_MODE_WIFI,
    APP_MODE_BLE,
    APP_MODE_ZIGBEE,
} app_mode_t;

/* ─── Long-running operation ─────────────────────────────────────────────── *
 * Allocated when a streaming or background operation starts.                 *
 * The task loops checking stop_requested; SYS_STOP sets the flag.           *
 * Set active_op = NULL once the task has cleaned up and exited.              */

typedef struct {
    TaskHandle_t  task;
    volatile bool stop_requested;
} active_op_t;

/* ─── Wi-Fi ──────────────────────────────────────────────────────────────── */

typedef enum {
    WIFI_SUB_NONE,
    WIFI_SUB_PROMISCUOUS,
    WIFI_SUB_CONNECTED,
} wifi_sub_mode_t;

typedef struct {
    char   ssid[33];   /* max 32-char SSID + null terminator */
    int8_t rssi;
    bool   open;       /* true = no password required */
} ssid_entry_t;

typedef struct {
    wifi_sub_mode_t  sub_mode;
    ssid_entry_t    *scan_results;   /* heap array; valid until next scan */
    uint8_t          ssid_count;
    char             connected_ip[16];
    char             connected_mac[18];
} wifi_state_t;

/* ─── BLE ────────────────────────────────────────────────────────────────── */

typedef struct {
    char   mac[18];
    char   name[32];
    int8_t rssi;
} ble_device_t;

typedef struct {
    ble_device_t *scan_results;   /* heap array; valid until next scan */
    uint8_t       device_count;
    char          tracked_mac[18];
} ble_state_t;

/* ─── ZigBee ─────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  channel;
    uint32_t packets;
} channel_stats_t;

typedef struct {
    uint8_t          active_channel;
    channel_stats_t *channel_stats;   /* heap array; valid until next scan */
    uint8_t          channel_count;
} zigbee_state_t;

/* ─── SD card ────────────────────────────────────────────────────────────── */

typedef struct {
    bool present;
    bool mounted;
} sd_state_t;

/* ─── Parsed UART command ────────────────────────────────────────────────── *
 * Tokens are the '_'-separated parts of the command name (e.g. WIFI,        *
 * PROMISCUOUS, SCAN). Args are the key="value" pairs that follow.            *
 * All char* pointers into a single scratch buffer owned by parsed_cmd_t.    */

#define PARSED_CMD_MAX_TOKENS  8
#define PARSED_CMD_MAX_ARGS   16

typedef struct {
    char *key;
    char *value;
} kv_pair_t;

typedef struct {
    char      *tokens[PARSED_CMD_MAX_TOKENS];
    int        token_count;
    kv_pair_t  args[PARSED_CMD_MAX_ARGS];
    int        arg_count;
    char       scratch[256];   /* raw line copy; tokens and arg pointers point here */
} parsed_cmd_t;

/* ─── Top-level application state ───────────────────────────────────────── *
 * Single source of truth. Initialised in main.c and passed by pointer       *
 * to every module that needs it.                                             */

typedef struct {
    app_mode_t     mode;
    active_op_t   *active_op;   /* NULL when no long-running operation is active */
    wifi_state_t   wifi;
    ble_state_t    ble;
    zigbee_state_t zigbee;
    sd_state_t     sd;
} app_state_t;
