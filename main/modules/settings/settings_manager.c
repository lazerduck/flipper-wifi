#include "modules/settings/settings_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "modules/sd/sd_card_manager.h"

#define SETTINGS_TAG       "settings_manager"
#define SETTINGS_DIR       "/settings"
#define SETTINGS_FILE_REL  "/settings/config.ini"
#define SETTINGS_FULL_PATH (SD_CARD_MOUNT_POINT SETTINGS_FILE_REL)
#define SETTINGS_LINE_MAX  192U

/* ---------------------------------------------------------------------------
 * Internal descriptor table
 * --------------------------------------------------------------------------- */

typedef struct {
    const char*     key;
    const char*     label;
    settings_type_t type;
    size_t          offset;
    int             default_value;
} settings_entry_t;

#define ENTRY_BOOL(k, lbl, field, def) \
    { .key = (k), .label = (lbl), .type = SETTINGS_TYPE_BOOL, \
      .offset = offsetof(settings_values_t, field), .default_value = (def) }

static const settings_entry_t s_entries[] = {
    ENTRY_BOOL("log_wifi_packets",  "Log WiFi Packets",     log_wifi_packets,  0),
    ENTRY_BOOL("log_ble_packets",   "Log BLE Packets",      log_ble_packets,   0),
    ENTRY_BOOL("ble_watch_recurring", "BLE Recurring Watch", ble_watch_recurring, 0),
};

#define SETTINGS_COUNT (sizeof(s_entries) / sizeof(s_entries[0]))

/* ---------------------------------------------------------------------------
 * Module state
 * --------------------------------------------------------------------------- */

typedef struct {
    bool              initialized;
    bool              sd_available;
    settings_values_t values;
    SemaphoreHandle_t mutex;
} settings_state_t;

static settings_state_t s_state;

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */

static void settings_apply_defaults(settings_values_t* v) {
    for(size_t i = 0U; i < SETTINGS_COUNT; i++) {
        const settings_entry_t* e = &s_entries[i];
        if(e->type == SETTINGS_TYPE_BOOL) {
            *((bool*)((uint8_t*)v + e->offset)) = (e->default_value != 0);
        }
    }
}

static bool settings_find_entry(const char* key, size_t* index_out) {
    if(key == NULL || index_out == NULL) {
        return false;
    }
    for(size_t i = 0U; i < SETTINGS_COUNT; i++) {
        if(strcmp(s_entries[i].key, key) == 0) {
            *index_out = i;
            return true;
        }
    }
    return false;
}

static esp_err_t settings_load(void) {
    FILE* f = fopen(SETTINGS_FULL_PATH, "r");
    if(f == NULL) {
        ESP_LOGW(SETTINGS_TAG, "config.ini not found — writing defaults");
        return ESP_ERR_NOT_FOUND;
    }

    char line[SETTINGS_LINE_MAX];
    while(fgets(line, (int)sizeof(line), f) != NULL) {
        /* Strip trailing CR/LF */
        size_t len = strlen(line);
        while(len > 0U && (line[len - 1U] == '\n' || line[len - 1U] == '\r')) {
            line[--len] = '\0';
        }

        if(len == 0U || line[0] == '#') {
            continue;
        }

        char* eq = strchr(line, '=');
        if(eq == NULL) {
            continue;
        }

        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;

        size_t idx;
        if(!settings_find_entry(key, &idx)) {
            ESP_LOGD(SETTINGS_TAG, "unknown key '%s' — skipped", key);
            continue;
        }

        const settings_entry_t* e = &s_entries[idx];
        if(e->type == SETTINGS_TYPE_BOOL) {
            if(strcmp(val, "1") == 0) {
                *((bool*)((uint8_t*)&s_state.values + e->offset)) = true;
            } else if(strcmp(val, "0") == 0) {
                *((bool*)((uint8_t*)&s_state.values + e->offset)) = false;
            } else {
                ESP_LOGW(
                    SETTINGS_TAG,
                    "invalid bool for '%s': '%s' — using default",
                    key,
                    val);
                *((bool*)((uint8_t*)&s_state.values + e->offset)) =
                    (e->default_value != 0);
            }
        }
    }

    fclose(f);
    ESP_LOGI(SETTINGS_TAG, "config loaded from %s", SETTINGS_FULL_PATH);
    return ESP_OK;
}

static esp_err_t settings_save(void) {
    if(!s_state.sd_available) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE* f = fopen(SETTINGS_FULL_PATH, "w");
    if(f == NULL) {
        ESP_LOGE(SETTINGS_TAG, "failed to open config.ini for writing");
        return ESP_FAIL;
    }

    fprintf(f, "# FuseRadio settings — auto-generated\n");

    for(size_t i = 0U; i < SETTINGS_COUNT; i++) {
        const settings_entry_t* e = &s_entries[i];
        if(e->type == SETTINGS_TYPE_BOOL) {
            bool v = *((const bool*)((const uint8_t*)&s_state.values + e->offset));
            fprintf(f, "%s=%d\n", e->key, v ? 1 : 0);
        }
    }

    fclose(f);
    ESP_LOGI(SETTINGS_TAG, "config saved to %s", SETTINGS_FULL_PATH);
    return ESP_OK;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

esp_err_t settings_manager_init(void) {
    if(s_state.initialized) {
        return ESP_OK;
    }

    s_state.mutex = xSemaphoreCreateMutex();
    if(s_state.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    settings_apply_defaults(&s_state.values);

    /* Attempt SD bring-up — idempotent, may already be up from sd_command */
    esp_err_t err = sd_card_manager_init();
    if(err != ESP_OK) {
        ESP_LOGW(
            SETTINGS_TAG,
            "SD init failed (0x%X) — using defaults, writes disabled",
            (unsigned)err);
        s_state.sd_available = false;
        s_state.initialized  = true;
        return ESP_OK;
    }

    /* Ensure /settings directory exists — also triggers card mount */
    err = sd_card_manager_ensure_directory(SETTINGS_DIR);
    if(err != ESP_OK) {
        ESP_LOGW(
            SETTINGS_TAG,
            "SD dir ensure failed (0x%X) — using defaults, writes disabled",
            (unsigned)err);
        s_state.sd_available = false;
        s_state.initialized  = true;
        return ESP_OK;
    }

    s_state.sd_available = true;

    err = settings_load();
    if(err == ESP_ERR_NOT_FOUND) {
        /* First boot — write defaults to card */
        settings_save();
    }

    s_state.initialized = true;
    return ESP_OK;
}

void settings_manager_deinit(void) {
    if(!s_state.initialized) {
        return;
    }
    if(s_state.mutex != NULL) {
        vSemaphoreDelete(s_state.mutex);
        s_state.mutex = NULL;
    }
    memset(&s_state, 0, sizeof(s_state));
}

bool settings_manager_sd_available(void) {
    return s_state.sd_available;
}

settings_values_t settings_manager_get(void) {
    settings_values_t snapshot;
    if(s_state.mutex != NULL) {
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    }
    snapshot = s_state.values;
    if(s_state.mutex != NULL) {
        xSemaphoreGive(s_state.mutex);
    }
    return snapshot;
}

esp_err_t settings_manager_set_bool(const char* key, bool value) {
    size_t idx;
    if(!settings_find_entry(key, &idx)) {
        return ESP_ERR_NOT_FOUND;
    }
    if(s_entries[idx].type != SETTINGS_TYPE_BOOL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(s_state.mutex != NULL) {
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    }
    *((bool*)((uint8_t*)&s_state.values + s_entries[idx].offset)) = value;
    if(s_state.mutex != NULL) {
        xSemaphoreGive(s_state.mutex);
    }

    if(!s_state.sd_available) {
        return ESP_ERR_INVALID_STATE;
    }

    return settings_save();
}

esp_err_t settings_manager_reset(void) {
    if(s_state.mutex != NULL) {
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    }
    settings_apply_defaults(&s_state.values);
    if(s_state.mutex != NULL) {
        xSemaphoreGive(s_state.mutex);
    }

    if(!s_state.sd_available) {
        return ESP_ERR_INVALID_STATE;
    }

    return settings_save();
}

void settings_manager_iterate(settings_iterator_t callback, void* context) {
    if(callback == NULL) {
        return;
    }

    settings_values_t snapshot = settings_manager_get();

    for(size_t i = 0U; i < SETTINGS_COUNT; i++) {
        settings_descriptor_t desc = {
            .key    = s_entries[i].key,
            .label  = s_entries[i].label,
            .type   = s_entries[i].type,
            .offset = s_entries[i].offset,
        };
        callback(&desc, &snapshot, context);
    }
}
