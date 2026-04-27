#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SD_CARD_MOUNT_POINT "/sdcard"
#define SD_CARD_LOGS_DIR "/logs"
#define SD_CARD_TABLES_DIR "/tables"

/* New board wiring */
#define SD_CARD_PIN_CS 4
#define SD_CARD_PIN_SCK 5
#define SD_CARD_PIN_MISO 6
#define SD_CARD_PIN_MOSI 7
#define SD_CARD_PIN_CD 2

typedef struct {
    bool card_present;
    bool mounted;
    uint64_t total_bytes;
    uint64_t free_bytes;
} sd_card_manager_info_t;

typedef struct {
    size_t entries_emitted;
    size_t entries_total;
    size_t entries_truncated;
} sd_card_manager_list_result_t;

typedef void (*sd_card_manager_writer_t)(const char* line, void* context);

esp_err_t sd_card_manager_init(void);
void sd_card_manager_deinit(void);
esp_err_t sd_card_manager_initialize(void);

esp_err_t sd_card_manager_get_info(sd_card_manager_info_t* info);
esp_err_t sd_card_manager_format(void);

esp_err_t sd_card_manager_list_directory(
    const char* relative_path,
    size_t max_entries,
    sd_card_manager_writer_t write_line,
    void* context,
    sd_card_manager_list_result_t* result);

esp_err_t sd_card_manager_ensure_directory(const char* relative_path);
esp_err_t sd_card_manager_append_text_file(const char* relative_path, const char* line);

/*
 * Hook for other modules: append one line to /logs/<module>.log.
 * The resolved relative path (for example /logs/wifi.log) is returned when requested.
 */
esp_err_t sd_card_manager_append_module_log(
    const char* module_name,
    const char* line,
    char* out_relative_path,
    size_t out_relative_path_size);
