#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "types.h"

/* Mount point used by all SD helpers.  Modules that build full paths should
 * use this rather than hardcoding "/sdcard".
 * e.g.  sd_write_file(SD_ROOT "/settings.json", buf); */
#if defined(UNIT_TEST)
#  define SD_ROOT "/tmp/test_sd"
#else
#  define SD_ROOT "/sdcard"
#endif

/*
 * sd_card.h — SD card mount, file I/O, and UART command handling.
 *
 * sd_init() attempts to mount the card over SPI.  It updates sd->present
 * and sd->mounted but never blocks the boot sequence — the card is optional.
 *
 * sd_handle_command() serves SD_STATUS, SD_LIST, SD_READ, SD_WRITE,
 * SD_APPEND, SD_MKDIR, SD_DELETE, and SD_FORMAT.
 *
 * The low-level helpers below operate on full absolute paths (including the
 * mount-point prefix, e.g. "/sdcard/settings.txt") and are used by other
 * modules such as settings.c.
 */

/* Attempt to mount the SD card; update sd->present and sd->mounted. */
void sd_init(sd_state_t *sd);

/* Route an SD_* UART command and send the response. */
void sd_handle_command(const parsed_cmd_t *cmd, sd_state_t *sd);

/* Read a file's contents into buf (null-terminated).
 * Returns false if the card is not mounted or the file does not exist. */
bool sd_read_file(const char *path, char *buf, size_t buf_len);

/* Write data to a file, creating or overwriting it.
 * Returns false if the write fails. */
bool sd_write_file(const char *path, const char *data);

/* Append data to a file, creating it if it does not exist.
 * Returns false if the write fails. */
bool sd_append_file(const char *path, const char *data);

/* Delete a file or empty directory.
 * Returns false if the path does not exist or the directory is not empty. */
bool sd_delete_file(const char *path);

/* Create a directory (non-recursive).
 * Returns false if creation fails. */
bool sd_mkdir(const char *path);
