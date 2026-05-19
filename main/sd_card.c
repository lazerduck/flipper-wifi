/* ── Platform headers ────────────────────────────────────────────────────── *
 * ESP-IDF-specific includes are guarded so the file compiles in host tests.  */
#ifndef UNIT_TEST
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#endif

#include "sd_card.h"
#include "uart.h"
#include "parser.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

/* Maximum characters read per line in SD_READ (fits in the UART line buffer) */
#define SD_LINE_MAX 255

/* Reject if not mounted and send the standard error response. */
#define RETURN_IF_NOT_MOUNTED(sd) \
    do { if (!(sd)->mounted) { uart_send_error("ERR_SD_NOT_MOUNTED"); return; } } while (0)

/* ── Low-level helpers ───────────────────────────────────────────────────── */

bool sd_read_file(const char *path, char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(buf, 1, buf_len - 1, f);
    buf[n] = '\0';
    fclose(f);
    return true;
}

bool sd_write_file(const char *path, const char *data)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    bool ok = fputs(data, f) != EOF;
    fclose(f);
    return ok;
}

bool sd_append_file(const char *path, const char *data)
{
    FILE *f = fopen(path, "a");
    if (!f) return false;
    bool ok = fputs(data, f) != EOF;
    fclose(f);
    return ok;
}

bool sd_delete_file(const char *path)
{
    return remove(path) == 0;
}

bool sd_mkdir(const char *path)
{
    return mkdir(path, 0755) == 0;
}

/* ── UART command handlers ───────────────────────────────────────────────── */

/* Forward declaration — defined after the hardware-init section. */
static void sd_cmd_remount(sd_state_t *sd);

static void sd_cmd_status(const sd_state_t *sd)
{
    char buf[128];
#ifndef UNIT_TEST
    unsigned long free_kb = 0, total_kb = 0;
    if (sd->mounted) {
        uint64_t total = 0, free_bytes = 0;
        esp_vfs_fat_info(SD_ROOT, &total, &free_bytes);
        free_kb  = (unsigned long)(free_bytes / 1024);
        total_kb = (unsigned long)(total       / 1024);
    }
    snprintf(buf, sizeof(buf),
             "present=\"%s\" mounted=\"%s\" free_kb=\"%lu\" total_kb=\"%lu\" fs_type=\"FAT32\"",
             sd->present ? "true" : "false",
             sd->mounted ? "true" : "false",
             free_kb, total_kb);
    (void)sd; /* suppress unused warning in non-UNIT_TEST path */
#else
    snprintf(buf, sizeof(buf),
             "present=\"%s\" mounted=\"%s\" free_kb=\"0\" total_kb=\"0\" fs_type=\"FAT32\"",
             sd->present ? "true" : "false",
             sd->mounted ? "true" : "false");
#endif
    uart_send_ok_with(buf);
}

static void sd_cmd_list(const parsed_cmd_t *cmd, const sd_state_t *sd)
{
    RETURN_IF_NOT_MOUNTED(sd);

    const char *path = parser_get_arg(cmd, "path");
    if (!path) path = "/";

    char full[256];
    snprintf(full, sizeof(full), "%s%s", SD_ROOT, path);

    DIR *dir = opendir(full);
    if (!dir) {
        uart_send_error("ERR_SD_PATH_NOT_FOUND");
        return;
    }

    uart_send_stream_start();
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; /* skip . and .. */

        bool is_dir;
        char entry_path[512]; /* full[255] + '/' + d_name[255] + null */

        snprintf(entry_path, sizeof(entry_path), "%s/%s", full, entry->d_name);

        if (entry->d_type == DT_UNKNOWN) {
            struct stat st;
            is_dir = (stat(entry_path, &st) == 0 && S_ISDIR(st.st_mode));
        } else {
            is_dir = (entry->d_type == DT_DIR);
        }

        long size = 0;
        if (!is_dir) {
            struct stat st;
            if (stat(entry_path, &st) == 0) size = (long)st.st_size;
        }

        char data_buf[384];
        snprintf(data_buf, sizeof(data_buf),
                 "name=\"%.*s\" size=\"%ld\" dir=\"%s\"",
                 255, entry->d_name, size, is_dir ? "true" : "false");
        uart_send_data(data_buf);
    }
    closedir(dir);
    uart_send_end();
}

static void sd_cmd_read(const parsed_cmd_t *cmd, const sd_state_t *sd)
{
    RETURN_IF_NOT_MOUNTED(sd);

    const char *path = parser_get_arg(cmd, "path");
    if (!path) { uart_send_error("ERR_MISSING_ARG"); return; }

    char full[256];
    snprintf(full, sizeof(full), "%s%s", SD_ROOT, path);

    FILE *f = fopen(full, "r");
    if (!f) {
        uart_send_error("ERR_SD_PATH_NOT_FOUND");
        return;
    }

    uart_send_stream_start();
    char line[SD_LINE_MAX + 2]; /* +2: newline + null */
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';

        char data_buf[SD_LINE_MAX + 16];
        snprintf(data_buf, sizeof(data_buf), "line=\"%s\"", line);
        uart_send_data(data_buf);
    }
    fclose(f);
    uart_send_end();
}

static void sd_cmd_write(const parsed_cmd_t *cmd, const sd_state_t *sd)
{
    RETURN_IF_NOT_MOUNTED(sd);
    const char *path = parser_get_arg(cmd, "path");
    const char *data = parser_get_arg(cmd, "data");
    if (!path || !data) { uart_send_error("ERR_MISSING_ARG"); return; }

    char full[256];
    snprintf(full, sizeof(full), "%s%s", SD_ROOT, path);

    sd_write_file(full, data) ? uart_send_ok() : uart_send_error("ERR_SD_WRITE_FAIL");
}

static void sd_cmd_append(const parsed_cmd_t *cmd, const sd_state_t *sd)
{
    RETURN_IF_NOT_MOUNTED(sd);
    const char *path = parser_get_arg(cmd, "path");
    const char *data = parser_get_arg(cmd, "data");
    if (!path || !data) { uart_send_error("ERR_MISSING_ARG"); return; }

    char full[256];
    snprintf(full, sizeof(full), "%s%s", SD_ROOT, path);

    sd_append_file(full, data) ? uart_send_ok() : uart_send_error("ERR_SD_WRITE_FAIL");
}

static void sd_cmd_mkdir(const parsed_cmd_t *cmd, const sd_state_t *sd)
{
    RETURN_IF_NOT_MOUNTED(sd);
    const char *path = parser_get_arg(cmd, "path");
    if (!path) { uart_send_error("ERR_MISSING_ARG"); return; }

    char full[256];
    snprintf(full, sizeof(full), "%s%s", SD_ROOT, path);

    sd_mkdir(full) ? uart_send_ok() : uart_send_error("ERR_SD_WRITE_FAIL");
}

static void sd_cmd_delete(const parsed_cmd_t *cmd, const sd_state_t *sd)
{
    RETURN_IF_NOT_MOUNTED(sd);
    const char *path = parser_get_arg(cmd, "path");
    if (!path) { uart_send_error("ERR_MISSING_ARG"); return; }

    char full[256];
    snprintf(full, sizeof(full), "%s%s", SD_ROOT, path);

    if (remove(full) == 0) { uart_send_ok(); return; }

    switch (errno) {
        case ENOTEMPTY: uart_send_error("ERR_SD_NOT_EMPTY");    break;
        case ENOENT:    uart_send_error("ERR_SD_PATH_NOT_FOUND"); break;
        default:        uart_send_error("ERR_SD_WRITE_FAIL");    break;
    }
}

static void sd_cmd_format(sd_state_t *sd)
{
    RETURN_IF_NOT_MOUNTED(sd);
#ifndef UNIT_TEST
    esp_err_t ret = esp_vfs_fat_sdcard_format(SD_ROOT, NULL);
    ret == ESP_OK ? uart_send_ok() : uart_send_error("ERR_SD_WRITE_FAIL");
#else
    uart_send_ok();
#endif
}

/* ── Public dispatch ─────────────────────────────────────────────────────── */

void sd_handle_command(const parsed_cmd_t *cmd, sd_state_t *sd)
{
    if (cmd->token_count < 2) {
        uart_send_error("ERR_UNKNOWN_CMD");
        return;
    }
    const char *sub = cmd->tokens[1];

    if      (strcmp(sub, "STATUS") == 0) sd_cmd_status(sd);
    else if (strcmp(sub, "LIST")   == 0) sd_cmd_list(cmd, sd);
    else if (strcmp(sub, "READ")   == 0) sd_cmd_read(cmd, sd);
    else if (strcmp(sub, "WRITE")  == 0) sd_cmd_write(cmd, sd);
    else if (strcmp(sub, "APPEND") == 0) sd_cmd_append(cmd, sd);
    else if (strcmp(sub, "MKDIR")  == 0) sd_cmd_mkdir(cmd, sd);
    else if (strcmp(sub, "DELETE") == 0) sd_cmd_delete(cmd, sd);
    else if (strcmp(sub, "FORMAT")  == 0) sd_cmd_format(sd);
    else if (strcmp(sub, "REMOUNT") == 0) sd_cmd_remount(sd);
    else                                   uart_send_error("ERR_UNKNOWN_CMD");
}

/* ── Hardware init (not compiled in unit tests) ──────────────────────────── */

#ifndef UNIT_TEST
static sdmmc_card_t *s_card        = NULL;
static bool          s_spi_ready   = false;

/* Attempt to (re-)mount the card.  Assumes gpio_config for CD pin was
 * already called (done once in sd_init).  Safe to call multiple times. */
static void sd_do_mount(sd_state_t *sd)
{
    sd->present = (gpio_get_level(CONFIG_FUSE_SD_CD_PIN) == 0);
    if (!sd->present) { sd->mounted = false; return; }

    if (!s_spi_ready) {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num     = CONFIG_FUSE_SD_MOSI_PIN,
            .miso_io_num     = CONFIG_FUSE_SD_MISO_PIN,
            .sclk_io_num     = CONFIG_FUSE_SD_SCK_PIN,
            .quadwp_io_num   = -1,
            .quadhd_io_num   = -1,
            .max_transfer_sz = 4096,
        };
        spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
        s_spi_ready = true;
    }

    sdmmc_host_t          host      = (sdmmc_host_t)SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_cfg  = (sdspi_device_config_t)SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs  = CONFIG_FUSE_SD_CS_PIN;
    slot_cfg.host_id  = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_ROOT, &host,
                                             &slot_cfg, &mount_cfg, &s_card);
    sd->mounted = (ret == ESP_OK);
}

void sd_init(sd_state_t *sd)
{
    sd->present = false;
    sd->mounted = false;

    /* Configure card-detect pin once (active low, internal pull-up). */
    gpio_config_t cd_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_FUSE_SD_CD_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cd_cfg);

    sd_do_mount(sd);
}
#endif /* UNIT_TEST */

/* ── Remount ─────────────────────────────────────────────────────────────── *
 * Unmounts the current card (if mounted), re-checks hardware presence, and  *
 * attempts a fresh mount.  Always responds S with present/mounted fields so  *
 * the Flipper app can update its UI regardless of outcome.                   */
static void sd_cmd_remount(sd_state_t *sd)
{
#ifndef UNIT_TEST
    if (sd->mounted) {
        esp_vfs_fat_sdcard_unmount(SD_ROOT, s_card);
        s_card     = NULL;
        sd->mounted = false;
        sd->present = false;
    }
    sd_do_mount(sd);
#endif
    char buf[64];
    snprintf(buf, sizeof(buf), "present=\"%s\" mounted=\"%s\"",
             sd->present ? "true" : "false",
             sd->mounted ? "true" : "false");
    uart_send_ok_with(buf);
}

