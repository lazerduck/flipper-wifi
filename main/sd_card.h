#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "types.h"

/*
 * sd_card.h — SD card mount, file I/O, and UART command handling.
 *
 * sd_init() attempts to mount the card over SPI.  It updates sd->present
 * and sd->mounted but never blocks the boot sequence — the card is optional.
 *
 * sd_handle_command() serves the SD_STATUS, SD_LIST, and SD_FORMAT commands.
 *
 * sd_read_file() and sd_write_file() are low-level helpers used by other
 * modules (e.g. settings.c) and are not exposed over UART directly.
 */

/* Attempt to mount the SD card; update sd->present and sd->mounted. */
void sd_init(sd_state_t *sd);

/* Route an SD_* UART command and send the response. */
void sd_handle_command(const parsed_cmd_t *cmd, sd_state_t *sd);

/* Read a file's contents into buf (null-terminated).
 * Returns false if the card is not mounted or the file does not exist. */
bool sd_read_file(const char *path, char *buf, size_t buf_len);

/* Write data to a file, creating or overwriting it.
 * Returns false if the card is not mounted or the write fails. */
bool sd_write_file(const char *path, const char *data);
