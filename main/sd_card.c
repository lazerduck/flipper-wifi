#include "sd_card.h"
#include "uart.h"

void sd_init(sd_state_t *sd)
{
    // TODO: configure SPI bus with pins from Kconfig; attempt sdmmc_card_init();
    //       set sd->present and sd->mounted; continue silently if card is absent
}

void sd_handle_command(const parsed_cmd_t *cmd, sd_state_t *sd)
{
    // TODO: inspect token after "SD"; dispatch to SD_STATUS, SD_LIST, SD_FORMAT handlers
    (void)cmd;
    (void)sd;
}

bool sd_read_file(const char *path, char *buf, size_t buf_len)
{
    // TODO: open path for reading; copy up to buf_len-1 bytes; null-terminate; return true on success
    (void)path;
    (void)buf;
    (void)buf_len;
    return false;
}

bool sd_write_file(const char *path, const char *data)
{
    // TODO: open path for writing (create or overwrite); write data; return true on success
    (void)path;
    (void)data;
    return false;
}
