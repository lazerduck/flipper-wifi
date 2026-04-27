#include "modules/sd/sd_card_manager.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SD_CARD_SPI_HOST SPI2_HOST
#define SD_CARD_SPI_FREQ_KHZ 400
#define SD_CARD_IGNORE_CD_FOR_DEBUG 1
#define SD_CARD_ENABLE_CMD0_PROBE 0
#define SD_CARD_MAX_FILES 8
#define SD_CARD_MAX_PATH 256
#define SD_CARD_LIST_LINE_MAX 256
#define SD_CARD_ENTRY_NAME_MAX_PRINT 96

static const char* SD_CARD_TAG = "sd_card";

typedef struct {
    bool initialized;
    bool mounted;
    sdmmc_host_t host;
    sdspi_device_config_t slot_config;
    sdmmc_card_t* card;
} sd_card_manager_state_t;

static sd_card_manager_state_t s_sd_state;

static esp_err_t sd_card_spi_transfer_byte(spi_device_handle_t dev, uint8_t tx, uint8_t* rx) {
    spi_transaction_t t = {0};
    uint8_t tx_local = tx;
    uint8_t rx_local = 0xFF;

    t.length = 8;
    t.tx_buffer = &tx_local;
    t.rx_buffer = &rx_local;

    esp_err_t err = spi_device_polling_transmit(dev, &t);
    if(err != ESP_OK) {
        return err;
    }

    if(rx != NULL) {
        *rx = rx_local;
    }

    return ESP_OK;
}

static void sd_card_spi_cmd0_probe(void) {
    spi_device_handle_t dev = NULL;
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 100000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    uint8_t cmd0[6] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
    uint8_t resp = 0xFF;
    esp_err_t err;

    // Manual CS handling lets us send the required >=74 idle clocks with CS high.
    gpio_set_direction(SD_CARD_PIN_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(SD_CARD_PIN_CS, 1);

    err = spi_bus_add_device(SD_CARD_SPI_HOST, &dev_cfg, &dev);
    if(err != ESP_OK) {
        ESP_LOGW(SD_CARD_TAG, "CMD0 probe: spi_bus_add_device failed: %s (0x%X)", esp_err_to_name(err), (unsigned)err);
        return;
    }

    // 80 clocks with CS high and DI high.
    for(size_t i = 0; i < 10; i++) {
        if(sd_card_spi_transfer_byte(dev, 0xFF, NULL) != ESP_OK) {
            ESP_LOGW(SD_CARD_TAG, "CMD0 probe: idle clocks failed");
            goto cleanup;
        }
    }

    gpio_set_level(SD_CARD_PIN_CS, 0);

    for(size_t i = 0; i < sizeof(cmd0); i++) {
        if(sd_card_spi_transfer_byte(dev, cmd0[i], NULL) != ESP_OK) {
            ESP_LOGW(SD_CARD_TAG, "CMD0 probe: command write failed");
            goto cleanup;
        }
    }

    // Wait for R1; expected 0x01 in idle state.
    resp = 0xFF;
    for(size_t i = 0; i < 10; i++) {
        if(sd_card_spi_transfer_byte(dev, 0xFF, &resp) != ESP_OK) {
            ESP_LOGW(SD_CARD_TAG, "CMD0 probe: response read failed");
            goto cleanup;
        }

        if(resp != 0xFF) {
            break;
        }
    }

    ESP_LOGI(SD_CARD_TAG, "CMD0 probe response: 0x%02X (expected 0x01)", resp);

cleanup:
    gpio_set_level(SD_CARD_PIN_CS, 1);
    (void)sd_card_spi_transfer_byte(dev, 0xFF, NULL);
    (void)spi_bus_remove_device(dev);
}

static bool sd_card_manager_card_present(void) {
    int level = gpio_get_level(SD_CARD_PIN_CD);
    return level == 0;
}

static esp_err_t sd_card_manager_prepare_gpio(void) {
    // Configure CD pin as input with pull-up
    gpio_config_t cd_cfg = {
        .pin_bit_mask = (1ULL << SD_CARD_PIN_CD),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&cd_cfg);
}

static void sd_card_manager_join_path(
    const char* relative_path,
    char* out_path,
    size_t out_path_size) {
    const char* suffix = "/";

    if(relative_path != NULL && relative_path[0] != '\0') {
        suffix = relative_path[0] == '/' ? relative_path : "";
    }

    if(relative_path == NULL || relative_path[0] == '\0') {
        snprintf(out_path, out_path_size, "%s/", SD_CARD_MOUNT_POINT);
        return;
    }

    if(relative_path[0] == '/') {
        snprintf(out_path, out_path_size, "%s%s", SD_CARD_MOUNT_POINT, relative_path);
    } else {
        snprintf(out_path, out_path_size, "%s/%s%s", SD_CARD_MOUNT_POINT, suffix, relative_path);
    }
}

static esp_err_t sd_card_manager_mount(bool format_if_mount_failed) {
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = format_if_mount_failed,
        .max_files = SD_CARD_MAX_FILES,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    esp_err_t err;
    static const int probe_freqs_khz[] = {400, 200, 100};

    if(s_sd_state.mounted) {
        return ESP_OK;
    }

    if(!SD_CARD_IGNORE_CD_FOR_DEBUG && !sd_card_manager_card_present()) {
        ESP_LOGW(SD_CARD_TAG, "card not present (CD pin high)");
        return ESP_ERR_NOT_FOUND;
    }

    if(SD_CARD_IGNORE_CD_FOR_DEBUG) {
        ESP_LOGW(SD_CARD_TAG, "CD check bypassed for debug");
    }

    ESP_LOGI(
        SD_CARD_TAG,
        "mounting SD over SPI: host=%d freq=%dkHz cs=%d sck=%d miso=%d mosi=%d cd=%d",
        (int)SD_CARD_SPI_HOST,
        SD_CARD_SPI_FREQ_KHZ,
        SD_CARD_PIN_CS,
        SD_CARD_PIN_SCK,
        SD_CARD_PIN_MISO,
        SD_CARD_PIN_MOSI,
        SD_CARD_PIN_CD);

    if(SD_CARD_ENABLE_CMD0_PROBE) {
        sd_card_spi_cmd0_probe();
    }

    // Give the card a short settle window before probing.
    vTaskDelay(pdMS_TO_TICKS(30));

    for(size_t i = 0; i < sizeof(probe_freqs_khz) / sizeof(probe_freqs_khz[0]); i++) {
        s_sd_state.host.max_freq_khz = probe_freqs_khz[i];
        ESP_LOGI(SD_CARD_TAG, "mount attempt %u at %dkHz", (unsigned)(i + 1), probe_freqs_khz[i]);

        err = esp_vfs_fat_sdspi_mount(
            SD_CARD_MOUNT_POINT,
            &s_sd_state.host,
            &s_sd_state.slot_config,
            &mount_cfg,
            &s_sd_state.card);

        if(err == ESP_OK) {
            ESP_LOGI(SD_CARD_TAG, "SD mount successful");
            s_sd_state.mounted = true;
            return ESP_OK;
        }

        ESP_LOGW(
            SD_CARD_TAG,
            "mount attempt %u failed: %s (0x%X)",
            (unsigned)(i + 1),
            esp_err_to_name(err),
            (unsigned)err);

        // Small delay between retries.
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGE(SD_CARD_TAG, "sdspi mount failed after retries: %s (0x%X)", esp_err_to_name(err), (unsigned)err);
    return err;
}

static esp_err_t sd_card_manager_ensure_mounted(void) {
    esp_err_t err;

    err = sd_card_manager_init();
    if(err != ESP_OK) {
        return err;
    }

    return sd_card_manager_mount(false);
}

static esp_err_t sd_card_manager_mkdir_recursive(const char* relative_path) {
    char path[SD_CARD_MAX_PATH];
    size_t length;

    if(relative_path == NULL || relative_path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    sd_card_manager_join_path(relative_path, path, sizeof(path));
    length = strlen(path);
    if(length == 0U || length >= sizeof(path) - 1U) {
        return ESP_ERR_INVALID_ARG;
    }

    for(size_t index = strlen(SD_CARD_MOUNT_POINT) + 1U; index < length; index++) {
        if(path[index] == '/') {
            path[index] = '\0';
            if(mkdir(path, 0775) != 0 && errno != EEXIST) {
                return ESP_FAIL;
            }
            path[index] = '/';
        }
    }

    if(mkdir(path, 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t sd_card_manager_init(void) {
    esp_err_t err;

    if(s_sd_state.initialized) {
        return ESP_OK;
    }

    err = sd_card_manager_prepare_gpio();
    if(err != ESP_OK) {
        ESP_LOGE(SD_CARD_TAG, "CD GPIO setup failed: %s (0x%X)", esp_err_to_name(err), (unsigned)err);
        return err;
    }

    // Initialize SPI bus - let the driver manage GPIO configuration
    // Don't pre-configure SPI pins as GPIO, let SPI driver handle it
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_CARD_PIN_MOSI,
        .miso_io_num = SD_CARD_PIN_MISO,
        .sclk_io_num = SD_CARD_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
        .flags = SPICOMMON_BUSFLAG_GPIO_PINS,  // Let SPI driver manage GPIO
    };

    err = spi_bus_initialize(SD_CARD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if(err != ESP_OK) {
        ESP_LOGE(SD_CARD_TAG, "SPI bus init failed: %s (0x%X)", esp_err_to_name(err), (unsigned)err);
        return err;
    }

    // After SPI bus is ready, configure host and slot
    s_sd_state.host = (sdmmc_host_t)SDSPI_HOST_DEFAULT();
    s_sd_state.host.slot = SD_CARD_SPI_HOST;
    s_sd_state.host.max_freq_khz = SD_CARD_SPI_FREQ_KHZ;

    // Initialize slot config using macro
    s_sd_state.slot_config = (sdspi_device_config_t)SDSPI_DEVICE_CONFIG_DEFAULT();
    s_sd_state.slot_config.host_id = SD_CARD_SPI_HOST;
    s_sd_state.slot_config.gpio_cs = SD_CARD_PIN_CS;
    // We handle card-detect in software via sd_card_manager_card_present().
    // Disable SDSPI's own CD handling to avoid state conflicts with socket switch wiring.
    s_sd_state.slot_config.gpio_cd = -1;
    s_sd_state.slot_config.gpio_wp = -1;

    ESP_LOGI(SD_CARD_TAG, "SD init ready: SPI bus initialized, device config set");

    s_sd_state.initialized = true;
    s_sd_state.mounted = false;
    s_sd_state.card = NULL;

    ESP_LOGI(SD_CARD_TAG, "SD init complete");
    return ESP_OK;
}

void sd_card_manager_deinit(void) {
    if(s_sd_state.mounted && s_sd_state.card != NULL) {
        esp_vfs_fat_sdcard_unmount(SD_CARD_MOUNT_POINT, s_sd_state.card);
        s_sd_state.mounted = false;
        s_sd_state.card = NULL;
    }

    if(s_sd_state.initialized) {
        spi_bus_free(SD_CARD_SPI_HOST);
    }

    memset(&s_sd_state, 0, sizeof(s_sd_state));
}

esp_err_t sd_card_manager_get_info(sd_card_manager_info_t* info) {
    struct statvfs statvfs_data;
    esp_err_t err;

    if(info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));
    info->card_present = sd_card_manager_card_present();

    err = sd_card_manager_ensure_mounted();
    if(err != ESP_OK) {
        return err;
    }

    info->mounted = true;

    if(statvfs(SD_CARD_MOUNT_POINT, &statvfs_data) == 0) {
        info->total_bytes = (uint64_t)statvfs_data.f_blocks * (uint64_t)statvfs_data.f_frsize;
        info->free_bytes = (uint64_t)statvfs_data.f_bavail * (uint64_t)statvfs_data.f_frsize;
    }

    // Some cards report zero through statvfs immediately after first-time init.
    // Fall back to decoded card capacity so UI still shows useful totals.
    if(info->total_bytes == 0U && s_sd_state.card != NULL && s_sd_state.card->csd.capacity > 0 &&
       s_sd_state.card->csd.sector_size > 0) {
        info->total_bytes =
            (uint64_t)s_sd_state.card->csd.capacity * (uint64_t)s_sd_state.card->csd.sector_size;
        if(info->free_bytes == 0U) {
            info->free_bytes = info->total_bytes;
        }
    }

    return ESP_OK;
}

esp_err_t sd_card_manager_initialize(void) {
    esp_err_t err;

    err = sd_card_manager_init();
    if(err != ESP_OK) {
        return err;
    }

    // First try normal mount. If this is a brand-new/unformatted card,
    // retry with format recovery enabled.
    err = sd_card_manager_mount(false);
    if(err != ESP_OK) {
        ESP_LOGW(
            SD_CARD_TAG,
            "initialize: normal mount failed, retrying with recovery: %s (0x%X)",
            esp_err_to_name(err),
            (unsigned)err);
        err = sd_card_manager_mount(true);
        if(err != ESP_OK) {
            return err;
        }
    }

    err = sd_card_manager_mkdir_recursive(SD_CARD_LOGS_DIR);
    if(err != ESP_OK) {
        return err;
    }

    err = sd_card_manager_mkdir_recursive(SD_CARD_TABLES_DIR);
    if(err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

esp_err_t sd_card_manager_format(void) {
    esp_err_t err = sd_card_manager_initialize();

    if(err != ESP_OK) {
        return err;
    }

    err = esp_vfs_fat_sdcard_format(SD_CARD_MOUNT_POINT, s_sd_state.card);
    if(err != ESP_OK) {
        return err;
    }

    err = sd_card_manager_mkdir_recursive(SD_CARD_LOGS_DIR);
    if(err != ESP_OK) {
        return err;
    }

    err = sd_card_manager_mkdir_recursive(SD_CARD_TABLES_DIR);
    if(err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

esp_err_t sd_card_manager_list_directory(
    const char* relative_path,
    size_t max_entries,
    sd_card_manager_writer_t write_line,
    void* context,
    sd_card_manager_list_result_t* result) {
    char path[SD_CARD_MAX_PATH];
    char line[SD_CARD_LIST_LINE_MAX];
    DIR* directory;
    struct dirent* entry;
    esp_err_t err;

    if(write_line == NULL || result == NULL || max_entries == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    err = sd_card_manager_ensure_mounted();
    if(err != ESP_OK) {
        return err;
    }

    memset(result, 0, sizeof(*result));
    sd_card_manager_join_path(relative_path, path, sizeof(path));

    directory = opendir(path);
    if(directory == NULL) {
        return ESP_FAIL;
    }

    while((entry = readdir(directory)) != NULL) {
        char entry_full_path[SD_CARD_MAX_PATH];
        struct stat st = {0};
        bool is_dir = false;
        int written = 0;

        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        result->entries_total++;
        if(result->entries_emitted >= max_entries) {
            result->entries_truncated++;
            continue;
        }

        written = snprintf(entry_full_path, sizeof(entry_full_path), "%s/%s", path, entry->d_name);
        if(written < 0 || (size_t)written >= sizeof(entry_full_path)) {
            result->entries_truncated++;
            continue;
        }
        if(stat(entry_full_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
        }

        snprintf(
            line,
            sizeof(line),
            "SD ENTRY type=%s size=%lu name=%.*s\n",
            is_dir ? "dir" : "file",
            is_dir ? 0UL : (unsigned long)st.st_size,
            (int)SD_CARD_ENTRY_NAME_MAX_PRINT,
            entry->d_name);
        write_line(line, context);
        result->entries_emitted++;
    }

    closedir(directory);
    return ESP_OK;
}

esp_err_t sd_card_manager_ensure_directory(const char* relative_path) {
    esp_err_t err = sd_card_manager_ensure_mounted();

    if(err != ESP_OK) {
        return err;
    }

    return sd_card_manager_mkdir_recursive(relative_path);
}

esp_err_t sd_card_manager_append_text_file(const char* relative_path, const char* line) {
    char full_path[SD_CARD_MAX_PATH];
    char parent_path[SD_CARD_MAX_PATH];
    char* parent_end;
    FILE* file;
    esp_err_t err;

    if(relative_path == NULL || relative_path[0] == '\0' || line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = sd_card_manager_ensure_mounted();
    if(err != ESP_OK) {
        return err;
    }

    sd_card_manager_join_path(relative_path, full_path, sizeof(full_path));
    snprintf(parent_path, sizeof(parent_path), "%s", full_path);
    parent_end = strrchr(parent_path, '/');
    if(parent_end != NULL && parent_end != parent_path) {
        *parent_end = '\0';
        if(strncmp(parent_path, SD_CARD_MOUNT_POINT, strlen(SD_CARD_MOUNT_POINT)) == 0) {
            const char* relative_parent = parent_path + strlen(SD_CARD_MOUNT_POINT);
            if(relative_parent[0] == '\0') {
                relative_parent = "/";
            }
            err = sd_card_manager_mkdir_recursive(relative_parent);
            if(err != ESP_OK) {
                return err;
            }
        }
    }

    file = fopen(full_path, "a");
    if(file == NULL) {
        return ESP_FAIL;
    }

    fprintf(file, "%s\n", line);
    fclose(file);
    return ESP_OK;
}

esp_err_t sd_card_manager_append_module_log(
    const char* module_name,
    const char* line,
    char* out_relative_path,
    size_t out_relative_path_size) {
    char safe_name[48];
    size_t out_index = 0U;
    char relative_path[96];

    if(module_name == NULL || module_name[0] == '\0' || line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for(size_t index = 0; module_name[index] != '\0' && out_index < sizeof(safe_name) - 1U; index++) {
        char current = module_name[index];

        if(isalnum((unsigned char)current) || current == '_' || current == '-') {
            safe_name[out_index++] = current;
        }
    }

    if(out_index == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    safe_name[out_index] = '\0';
    snprintf(relative_path, sizeof(relative_path), "%s/%s.log", SD_CARD_LOGS_DIR, safe_name);

    esp_err_t err = sd_card_manager_append_text_file(relative_path, line);
    if(err != ESP_OK) {
        return err;
    }

    if(out_relative_path != NULL && out_relative_path_size > 0U) {
        snprintf(out_relative_path, out_relative_path_size, "%s", relative_path);
    }

    return ESP_OK;
}
