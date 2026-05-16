#include "modules/zigbee/zigbee_profile_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "modules/sd/sd_card_manager.h"

#define ZIGBEE_STORE_TAG "zigbee_store"
#define ZIGBEE_STORE_DIR "/tables/zigbee"
#define ZIGBEE_STORE_FILE_REL "/tables/zigbee/profiles_v1.txt"
#define ZIGBEE_STORE_FILE_FULL (SD_CARD_MOUNT_POINT ZIGBEE_STORE_FILE_REL)
#define ZIGBEE_STORE_FILE_TMP_FULL (SD_CARD_MOUNT_POINT "/tables/zigbee/profiles_v1.txt.tmp")
#define ZIGBEE_STORE_LINE_MAX 256U

static void zigbee_profile_store_trim_eol(char* line) {
    size_t len;

    if(line == NULL) {
        return;
    }

    len = strlen(line);
    while(len > 0U && (line[len - 1U] == '\n' || line[len - 1U] == '\r')) {
        line[--len] = '\0';
    }
}

static void zigbee_profile_store_set_default_buttons(zigbee_profile_t* profile) {
    size_t i;

    if(profile == NULL) {
        return;
    }

    for(i = 0U; i < ZIGBEE_BUTTONS_PER_PROFILE; i++) {
        snprintf(profile->buttons[i].name, sizeof(profile->buttons[i].name), "Button %u", (unsigned)(i + 1U));
    }
}

static bool zigbee_profile_store_parse_u32(const char* token, uint32_t* out) {
    char* end = NULL;
    unsigned long value;

    if(token == NULL || out == NULL || token[0] == '\0') {
        return false;
    }

    value = strtoul(token, &end, 10);
    if(end == NULL || *end != '\0') {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static bool zigbee_profile_store_parse_u16(const char* token, uint16_t* out) {
    uint32_t value;

    if(!zigbee_profile_store_parse_u32(token, &value) || value > 0xFFFFU) {
        return false;
    }

    *out = (uint16_t)value;
    return true;
}

static bool zigbee_profile_store_parse_u8(const char* token, uint8_t* out) {
    uint32_t value;

    if(!zigbee_profile_store_parse_u32(token, &value) || value > 0xFFU) {
        return false;
    }

    *out = (uint8_t)value;
    return true;
}

static zigbee_profile_t* zigbee_profile_store_find_by_id(
    zigbee_profile_t* profiles,
    size_t profile_count,
    uint32_t id) {
    size_t i;

    for(i = 0U; i < profile_count; i++) {
        if(profiles[i].id == id) {
            return &profiles[i];
        }
    }

    return NULL;
}

esp_err_t zigbee_profile_store_load(
    zigbee_profile_t* profiles,
    size_t max_profiles,
    size_t* out_count,
    uint32_t* out_next_id,
    bool* out_sd_available) {
    FILE* f = NULL;
    char line[ZIGBEE_STORE_LINE_MAX];
    size_t count = 0U;
    uint32_t next_id = 1U;

    if(profiles == NULL || out_count == NULL || out_next_id == NULL || out_sd_available == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_sd_available = false;

    if(sd_card_manager_init() != ESP_OK) {
        *out_count = 0U;
        *out_next_id = next_id;
        return ESP_OK;
    }

    if(sd_card_manager_ensure_directory(SD_CARD_TABLES_DIR) != ESP_OK ||
       sd_card_manager_ensure_directory(ZIGBEE_STORE_DIR) != ESP_OK) {
        *out_count = 0U;
        *out_next_id = next_id;
        return ESP_OK;
    }

    *out_sd_available = true;

    f = fopen(ZIGBEE_STORE_FILE_FULL, "r");
    if(f == NULL) {
        *out_count = 0U;
        *out_next_id = next_id;
        return ESP_OK;
    }

    while(fgets(line, (int)sizeof(line), f) != NULL) {
        char* token = NULL;

        zigbee_profile_store_trim_eol(line);

        if(line[0] == '\0' || line[0] == '#') {
            continue;
        }

        token = strtok(line, "\t");
        if(token == NULL) {
            continue;
        }

        if(strcmp(token, "VERSION") == 0) {
            continue;
        }

        if(strcmp(token, "NEXT_ID") == 0) {
            char* value = strtok(NULL, "\t");
            uint32_t parsed;
            if(value != NULL && zigbee_profile_store_parse_u32(value, &parsed) && parsed > 0U) {
                next_id = parsed;
            }
            continue;
        }

        if(strcmp(token, "P") == 0) {
            char* id_token = strtok(NULL, "\t");
            char* pan_token = strtok(NULL, "\t");
            char* ch_token = strtok(NULL, "\t");
            char* joined_token = strtok(NULL, "\t");
            char* name_token = strtok(NULL, "\t");
            zigbee_profile_t* profile = NULL;
            uint32_t profile_id;
            uint16_t pan_id;
            uint8_t channel;
            uint32_t joined_num;

            if(id_token == NULL || pan_token == NULL || ch_token == NULL ||
               joined_token == NULL || name_token == NULL) {
                continue;
            }

            if(!zigbee_profile_store_parse_u32(id_token, &profile_id) ||
               !zigbee_profile_store_parse_u16(pan_token, &pan_id) ||
               !zigbee_profile_store_parse_u8(ch_token, &channel) ||
               !zigbee_profile_store_parse_u32(joined_token, &joined_num)) {
                continue;
            }

            if(count >= max_profiles) {
                continue;
            }

            profile = &profiles[count++];
            memset(profile, 0, sizeof(*profile));
            profile->id = profile_id;
            profile->pan_id = pan_id;
            profile->channel = channel;
            profile->joined = joined_num != 0U;
            snprintf(profile->name, sizeof(profile->name), "%s", name_token);
            zigbee_profile_store_set_default_buttons(profile);

            if(profile_id >= next_id) {
                next_id = profile_id + 1U;
            }

            continue;
        }

        if(strcmp(token, "B") == 0) {
            char* id_token = strtok(NULL, "\t");
            char* index_token = strtok(NULL, "\t");
            char* name_token = strtok(NULL, "\t");
            uint32_t profile_id;
            uint32_t button_index;
            zigbee_profile_t* profile = NULL;

            if(id_token == NULL || index_token == NULL || name_token == NULL) {
                continue;
            }

            if(!zigbee_profile_store_parse_u32(id_token, &profile_id) ||
               !zigbee_profile_store_parse_u32(index_token, &button_index) ||
               button_index >= ZIGBEE_BUTTONS_PER_PROFILE) {
                continue;
            }

            profile = zigbee_profile_store_find_by_id(profiles, count, profile_id);
            if(profile == NULL) {
                continue;
            }

            snprintf(profile->buttons[button_index].name, sizeof(profile->buttons[button_index].name), "%s", name_token);
        }
    }

    fclose(f);
    *out_count = count;
    *out_next_id = next_id;
    return ESP_OK;
}

esp_err_t zigbee_profile_store_save(
    const zigbee_profile_t* profiles,
    size_t profile_count,
    uint32_t next_id) {
    FILE* f = NULL;
    size_t i;
    size_t button_index;

    if(profiles == NULL && profile_count != 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if(sd_card_manager_init() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    if(sd_card_manager_ensure_directory(SD_CARD_TABLES_DIR) != ESP_OK ||
       sd_card_manager_ensure_directory(ZIGBEE_STORE_DIR) != ESP_OK) {
        return ESP_FAIL;
    }

    f = fopen(ZIGBEE_STORE_FILE_TMP_FULL, "w");
    if(f == NULL) {
        ESP_LOGE(ZIGBEE_STORE_TAG, "failed to open temp profile store");
        return ESP_FAIL;
    }

    fprintf(f, "VERSION\t1\n");
    fprintf(f, "NEXT_ID\t%u\n", (unsigned)next_id);

    for(i = 0U; i < profile_count; i++) {
        const zigbee_profile_t* profile = &profiles[i];
        fprintf(
            f,
            "P\t%u\t%u\t%u\t%u\t%s\n",
            (unsigned)profile->id,
            (unsigned)profile->pan_id,
            (unsigned)profile->channel,
            profile->joined ? 1U : 0U,
            profile->name);

        for(button_index = 0U; button_index < ZIGBEE_BUTTONS_PER_PROFILE; button_index++) {
            fprintf(
                f,
                "B\t%u\t%u\t%s\n",
                (unsigned)profile->id,
                (unsigned)button_index,
                profile->buttons[button_index].name);
        }
    }

    fclose(f);

    if(rename(ZIGBEE_STORE_FILE_TMP_FULL, ZIGBEE_STORE_FILE_FULL) != 0) {
        remove(ZIGBEE_STORE_FILE_TMP_FULL);
        ESP_LOGE(ZIGBEE_STORE_TAG, "failed to swap profile store file");
        return ESP_FAIL;
    }

    return ESP_OK;
}
