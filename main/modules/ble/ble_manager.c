#include "modules/ble/ble_manager.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define BLE_MANAGER_DEFAULT_SCAN_DURATION_MS 4500
#define BLE_MANAGER_MIN_SCAN_DURATION_MS     2000
#define BLE_MANAGER_MAX_SCAN_DURATION_MS     30000
#define BLE_MANAGER_INIT_WAIT_MS             7000
#define BLE_MANAGER_LINE_SIZE        256
#define BLE_MANAGER_NAME_SIZE        33
#define BLE_MANAGER_COMPANY_SIZE     20
#define BLE_MANAGER_APPEARANCE_SIZE  18
#define BLE_MANAGER_CLASS_SIZE       16
#define BLE_MANAGER_PROX_SIZE        10
#define BLE_MANAGER_UUID16_CAP       8

#define BLE_AD_TYPE_UUID16_INCOMPLETE 0x02
#define BLE_AD_TYPE_UUID16_COMPLETE   0x03
#define BLE_AD_TYPE_NAME_SHORT        0x08
#define BLE_AD_TYPE_NAME_COMPLETE     0x09
#define BLE_AD_TYPE_TX_POWER          0x0A
#define BLE_AD_TYPE_SERVICE_DATA16    0x16
#define BLE_AD_TYPE_APPEARANCE        0x19
#define BLE_AD_TYPE_MANUFACTURER      0xFF

typedef struct {
    uint16_t company_id;
    uint16_t appearance;
    uint16_t uuid16s[BLE_MANAGER_UUID16_CAP];
    uint8_t uuid16_count;
    int8_t tx_power;
    bool has_company;
    bool has_appearance;
    bool has_tx_power;
    bool is_ibeacon;
    bool is_eddystone;
} ble_adv_summary_t;

typedef struct {
    ble_addr_t address;
    char name[BLE_MANAGER_NAME_SIZE];
    char company[BLE_MANAGER_COMPANY_SIZE];
    char appearance[BLE_MANAGER_APPEARANCE_SIZE];
    char classification[BLE_MANAGER_CLASS_SIZE];
    char proximity[BLE_MANAGER_PROX_SIZE];
    int8_t rssi;
    int8_t tx_power;
    bool has_name;
    bool has_company;
    bool has_appearance;
    bool has_tx_power;
    bool connectable;
    bool in_use;
} ble_scan_result_t;

typedef struct {
    ble_scan_result_t results[BLE_MANAGER_MAX_RESULTS];
    SemaphoreHandle_t done_semaphore;
    uint16_t total_count;
    int completion_reason;
} ble_scan_session_t;

typedef struct {
    uint16_t company_id;
    const char *name;
} ble_company_lookup_t;

static bool s_ble_initialized = false;
static bool s_ble_synced = false;
static SemaphoreHandle_t s_ble_sync_semaphore = NULL;
static SemaphoreHandle_t s_ble_scan_mutex = NULL;

static const ble_company_lookup_t s_ble_company_lookup[] = {
    {0x0002, "Intel"},
    {0x0006, "Microsoft"},
    {0x0009, "Infineon"},
    {0x000A, "Qualcomm"},
    {0x000D, "TI"},
    {0x000F, "Broadcom"},
    {0x001D, "Qualcomm"},
    {0x0025, "NXP"},
    {0x002F, "ST"},
    {0x0046, "MediaTek"},
    {0x004C, "Apple"},
    {0x0057, "HARMAN"},
    {0x0059, "Nordic"},
    {0x005C, "Belkin"},
    {0x005D, "Realtek"},
    {0x0065, "HP"},
    {0x0075, "Samsung"},
    {0x0087, "Garmin"},
    {0x009E, "Bose"},
    {0x00A0, "Kensington"},
    {0x00C4, "LG"},
    {0x00CC, "Beats"},
    {0x00CD, "Microchip"},
    {0x00D7, "Qualcomm"},
    {0x00D8, "Qualcomm"},
    {0x00E0, "Google"},
    {0x012D, "Sony"},
    {0x0131, "Cypress"},
    {0x013A, "Tencent"},
    {0x013C, "Murata"},
    {0x0155, "Netatmo"},
    {0x0171, "Amazon"},
    {0x018E, "Google"},
    {0x01AB, "Meta"},
    {0x01DA, "Logitech"},
    {0x01DD, "Philips"},
    {0x021B, "Cisco"},
    {0x022B, "Tesla"},
    {0x027D, "Huawei"},
    {0x02A6, "Bosch"},
    {0x02B6, "Schneider"},
    {0x02C5, "Lenovo"},
    {0x02D5, "Omron"},
    {0x02E5, "Espressif"},
    {0x02FF, "Silabs"},
    {0x0304, "Oura"},
    {0x038F, "Xiaomi"},
    {0x03FF, "Withings"},
    {0x041E, "Dell"},
    {0x0446, "Netgear"},
    {0x0494, "Sennheiser"},
    {0x04AD, "Shure"},
    {0x04EC, "Motorola"},
    {0x050C, "OSRAM"},
    {0x0526, "Honeywell"},
    {0x0553, "Nintendo"},
    {0x055D, "Valve"},
    {0x0586, "Legrand"},
    {0x058E, "Meta"},
    {0x05A7, "Sonos"},
    {0x0600, "iRobot"},
    {0x060F, "Signify"},
    {0x0618, "AudioTech"},
    {0x067C, "Tile"},
    {0x067D, "Fitbit"},
    {0x0687, "Cherry"},
    {0x068E, "Razer"},
    {0x072F, "OnePlus"},
    {0x079A, "OPPO"},
    {0x080B, "Nanoleaf"},
    {0x083F, "D-Link"},
    {0x0870, "Wyze"},
    {0x08B4, "Sengled"},
    {0x08C3, "Chipolo"},
    {0x08DC, "Aukey"},
    {0x08EE, "Askey"},
    {0x0958, "ZTE"},
    {0x09A3, "Arduino"},
    {0x09C6, "Honor"},
    {0x09E1, "Tag-N-Trac"},
    {0x0A12, "Dyson"},
    {0x0A82, "Corsair"},
    {0x0B27, "Aqara"},
    {0x0B93, "BroadLink"},
    {0x0BEE, "Linksys"},
    {0x0C19, "Arlo"},
    {0x0C8C, "T-Mobile"},
    {0x0CAC, "Shokz"},
    {0x0CC2, "Anker"},
    {0x0CC3, "HMD"},
    {0x0CCB, "Nothing"},
    {0x0D68, "StatusAudio"},
    {0x0D72, "EarFun"},
    {0x0E29, "Flipper"},
    {0x0E41, "ASUS"},
};

static const uint16_t s_ble_tracker_company_ids[] = {0x0518, 0x067C, 0x08C3, 0x09E1};
static const uint16_t s_ble_audio_company_ids[] = {
    0x0057, 0x009E, 0x00CC, 0x0494, 0x04AD, 0x05A7, 0x0618, 0x0CAC, 0x0D68, 0x0D72};
static const uint16_t s_ble_input_company_ids[] = {0x00A0, 0x0111, 0x01DA, 0x0687, 0x068E};
static const uint16_t s_ble_light_company_ids[] = {0x01DD, 0x050C, 0x060F, 0x080B, 0x08B4, 0x0B27};
static const uint16_t s_ble_network_company_ids[] = {0x021B, 0x0446, 0x083F, 0x0B93, 0x0BEE};

static const char *ble_manager_addr_type_to_string(uint8_t type)
{
    switch (type) {
    case BLE_ADDR_PUBLIC:
        return "PUBLIC";
    case BLE_ADDR_RANDOM:
        return "RANDOM";
    case BLE_ADDR_PUBLIC_ID:
        return "PUBLIC_ID";
    case BLE_ADDR_RANDOM_ID:
        return "RANDOM_ID";
    default:
        return "UNKNOWN";
    }
}

static bool ble_manager_event_is_connectable(uint8_t event_type)
{
    return event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
           event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
}

static uint16_t ble_manager_read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static char ble_manager_ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (char)(value + ('a' - 'A'));
    }

    return value;
}

static void ble_manager_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0 || src == NULL) {
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static void ble_manager_copy_name(char *dst, size_t dst_size, const uint8_t *src, size_t src_len)
{
    size_t copy_length;

    if (dst == NULL || dst_size == 0 || src == NULL || src_len == 0) {
        return;
    }

    copy_length = src_len;
    if (copy_length >= dst_size) {
        copy_length = dst_size - 1U;
    }

    memcpy(dst, src, copy_length);
    dst[copy_length] = '\0';
}

static void ble_manager_append_uuid16(ble_adv_summary_t *summary, uint16_t uuid16)
{
    size_t index;

    if (summary == NULL) {
        return;
    }

    for (index = 0; index < summary->uuid16_count; ++index) {
        if (summary->uuid16s[index] == uuid16) {
            return;
        }
    }

    if (summary->uuid16_count < BLE_MANAGER_UUID16_CAP) {
        summary->uuid16s[summary->uuid16_count++] = uuid16;
    }
}

static const char *ble_manager_lookup_company_name(uint16_t company_id)
{
    size_t index;

    for (index = 0; index < (sizeof(s_ble_company_lookup) / sizeof(s_ble_company_lookup[0])); ++index) {
        if (s_ble_company_lookup[index].company_id == company_id) {
            return s_ble_company_lookup[index].name;
        }
    }

    return NULL;
}

static bool ble_manager_company_id_in_list(
    uint16_t company_id,
    const uint16_t *company_ids,
    size_t company_count)
{
    size_t index;

    if (company_ids == NULL) {
        return false;
    }

    for (index = 0; index < company_count; ++index) {
        if (company_ids[index] == company_id) {
            return true;
        }
    }

    return false;
}

static bool ble_manager_text_contains(const char *text, const char *needle)
{
    size_t offset;
    size_t needle_length;

    if (text == NULL || needle == NULL || text[0] == '\0' || needle[0] == '\0') {
        return false;
    }

    needle_length = strlen(needle);
    for (offset = 0; text[offset] != '\0'; ++offset) {
        size_t index = 0;

        while (index < needle_length && text[offset + index] != '\0' &&
               ble_manager_ascii_lower(text[offset + index]) == ble_manager_ascii_lower(needle[index])) {
            ++index;
        }

        if (index == needle_length) {
            return true;
        }
    }

    return false;
}

static bool ble_manager_name_matches_any(
    const ble_scan_result_t *result,
    const char *const *fragments,
    size_t fragment_count)
{
    size_t index;

    if (result == NULL || !result->has_name || fragments == NULL) {
        return false;
    }

    for (index = 0; index < fragment_count; ++index) {
        if (ble_manager_text_contains(result->name, fragments[index])) {
            return true;
        }
    }

    return false;
}

static void ble_manager_company_name(uint16_t company_id, char *buffer, size_t buffer_size)
{
    const char *company_name;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    company_name = ble_manager_lookup_company_name(company_id);
    if (company_name != NULL) {
        ble_manager_copy_text(buffer, buffer_size, company_name);
    } else {
        snprintf(buffer, buffer_size, "CID%04X", company_id);
    }
}

static void ble_manager_appearance_name(uint16_t appearance, char *buffer, size_t buffer_size)
{
    uint16_t category;
    uint16_t subcategory;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    category = appearance >> 6;
    subcategory = appearance & 0x3FU;

    switch (category) {
    case 1:
        ble_manager_copy_text(buffer, buffer_size, "Phone");
        break;
    case 2:
        ble_manager_copy_text(buffer, buffer_size, "Computer");
        break;
    case 3:
        ble_manager_copy_text(buffer, buffer_size, "Watch");
        break;
    case 4:
        ble_manager_copy_text(buffer, buffer_size, "Clock");
        break;
    case 5:
        ble_manager_copy_text(buffer, buffer_size, "Display");
        break;
    case 6:
        ble_manager_copy_text(buffer, buffer_size, "Remote");
        break;
    case 7:
        ble_manager_copy_text(buffer, buffer_size, "Eyewear");
        break;
    case 8:
        ble_manager_copy_text(buffer, buffer_size, "Tag");
        break;
    case 9:
        ble_manager_copy_text(buffer, buffer_size, "Keyring");
        break;
    case 10:
        ble_manager_copy_text(buffer, buffer_size, "Media");
        break;
    case 12:
        ble_manager_copy_text(buffer, buffer_size, "Thermo");
        break;
    case 13:
        ble_manager_copy_text(buffer, buffer_size, "Heart");
        break;
    case 14:
        ble_manager_copy_text(buffer, buffer_size, "Blood");
        break;
    case 15:
        switch (subcategory) {
        case 1:
            ble_manager_copy_text(buffer, buffer_size, "Keyboard");
            break;
        case 2:
            ble_manager_copy_text(buffer, buffer_size, "Mouse");
            break;
        case 3:
            ble_manager_copy_text(buffer, buffer_size, "Joystick");
            break;
        case 4:
            ble_manager_copy_text(buffer, buffer_size, "Gamepad");
            break;
        default:
            ble_manager_copy_text(buffer, buffer_size, "Input");
            break;
        }
        break;
    case 16:
    case 17:
    case 18:
        ble_manager_copy_text(buffer, buffer_size, "Fitness");
        break;
    case 21:
        ble_manager_copy_text(buffer, buffer_size, "Sensor");
        break;
    case 22:
    case 31:
        ble_manager_copy_text(buffer, buffer_size, "Light");
        break;
    case 20:
        ble_manager_copy_text(buffer, buffer_size, "Network");
        break;
    case 30:
        ble_manager_copy_text(buffer, buffer_size, "Power");
        break;
    case 35:
        ble_manager_copy_text(buffer, buffer_size, "Pulse");
        break;
    case 50:
        ble_manager_copy_text(buffer, buffer_size, "Scale");
        break;
    default:
        snprintf(buffer, buffer_size, "APP%04X", appearance);
        break;
    }
}

static bool ble_manager_classify_from_name(
    const ble_scan_result_t *result,
    char *buffer,
    size_t buffer_size)
{
    static const char *const tracker_fragments[] = {
        "airtag", "chipolo", "tile", "tag", "tracker", "track"};
    static const char *const audio_fragments[] = {
        "airpods", "buds", "pod", "ear", "head", "speaker", "audio", "sound", "jbl"};
    static const char *const lighting_fragments[] = {
        "hue", "lamp", "light", "bulb", "led", "nanoleaf"};
    static const char *const sensor_fragments[] = {
        "sensor", "thermo", "temp", "humid", "scale", "meter", "co2", "air"};
    static const char *const input_fragments[] = {
        "keyboard", "mouse", "gamepad", "controller", "stylus", "trackpad", "pen"};
    static const char *const access_fragments[] = {
        "lock", "door", "key", "safe", "entry"};
    static const char *const wearable_fragments[] = {
        "watch", "band", "ring", "fit", "garmin", "whoop", "versa"};
    static const char *const network_fragments[] = {
        "router", "mesh", "gateway", "bridge", "wifi", "net"};

    if (ble_manager_name_matches_any(
            result,
            tracker_fragments,
            sizeof(tracker_fragments) / sizeof(tracker_fragments[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Tracker");
        return true;
    }

    if (ble_manager_name_matches_any(
            result,
            audio_fragments,
            sizeof(audio_fragments) / sizeof(audio_fragments[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Audio");
        return true;
    }

    if (ble_manager_name_matches_any(
            result,
            lighting_fragments,
            sizeof(lighting_fragments) / sizeof(lighting_fragments[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Lighting");
        return true;
    }

    if (ble_manager_name_matches_any(
            result,
            sensor_fragments,
            sizeof(sensor_fragments) / sizeof(sensor_fragments[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Sensor");
        return true;
    }

    if (ble_manager_name_matches_any(
            result,
            input_fragments,
            sizeof(input_fragments) / sizeof(input_fragments[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Input");
        return true;
    }

    if (ble_manager_name_matches_any(
            result,
            access_fragments,
            sizeof(access_fragments) / sizeof(access_fragments[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Access");
        return true;
    }

    if (ble_manager_name_matches_any(
            result,
            wearable_fragments,
            sizeof(wearable_fragments) / sizeof(wearable_fragments[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Wearable");
        return true;
    }

    if (ble_manager_name_matches_any(
            result,
            network_fragments,
            sizeof(network_fragments) / sizeof(network_fragments[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Network");
        return true;
    }

    return false;
}

static bool ble_manager_classify_from_company(
    const ble_adv_summary_t *summary,
    char *buffer,
    size_t buffer_size)
{
    if (summary == NULL || !summary->has_company) {
        return false;
    }

    if (ble_manager_company_id_in_list(
            summary->company_id,
            s_ble_tracker_company_ids,
            sizeof(s_ble_tracker_company_ids) / sizeof(s_ble_tracker_company_ids[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Tracker");
        return true;
    }

    if (ble_manager_company_id_in_list(
            summary->company_id,
            s_ble_audio_company_ids,
            sizeof(s_ble_audio_company_ids) / sizeof(s_ble_audio_company_ids[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Audio");
        return true;
    }

    if (ble_manager_company_id_in_list(
            summary->company_id,
            s_ble_input_company_ids,
            sizeof(s_ble_input_company_ids) / sizeof(s_ble_input_company_ids[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Input");
        return true;
    }

    if (ble_manager_company_id_in_list(
            summary->company_id,
            s_ble_light_company_ids,
            sizeof(s_ble_light_company_ids) / sizeof(s_ble_light_company_ids[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Lighting");
        return true;
    }

    if (ble_manager_company_id_in_list(
            summary->company_id,
            s_ble_network_company_ids,
            sizeof(s_ble_network_company_ids) / sizeof(s_ble_network_company_ids[0]))) {
        ble_manager_copy_text(buffer, buffer_size, "Network");
        return true;
    }

    switch (summary->company_id) {
    case 0x004C:
    case 0x0075:
    case 0x00E0:
    case 0x018E:
    case 0x012D:
    case 0x038F:
    case 0x072F:
    case 0x079A:
    case 0x0CCB:
    case 0x027D:
    case 0x01AB:
    case 0x058E:
        ble_manager_copy_text(buffer, buffer_size, "Personal");
        return true;
    default:
        return false;
    }
}

static void ble_manager_classification_name(
    const ble_scan_result_t *result,
    const ble_adv_summary_t *summary,
    char *buffer,
    size_t buffer_size)
{
    size_t index;

    if (buffer == NULL || buffer_size == 0 || result == NULL || summary == NULL) {
        return;
    }

    if (summary->is_ibeacon || summary->is_eddystone) {
        ble_manager_copy_text(buffer, buffer_size, "Beacon");
        return;
    }

    if (ble_manager_classify_from_name(result, buffer, buffer_size)) {
        return;
    }

    if (result->has_appearance) {
        if (strcmp(result->appearance, "Keyboard") == 0 ||
            strcmp(result->appearance, "Mouse") == 0 ||
            strcmp(result->appearance, "Joystick") == 0 ||
            strcmp(result->appearance, "Gamepad") == 0 ||
            strcmp(result->appearance, "Input") == 0) {
            ble_manager_copy_text(buffer, buffer_size, "Input");
            return;
        }
        if (strcmp(result->appearance, "Tag") == 0 || strcmp(result->appearance, "Keyring") == 0) {
            ble_manager_copy_text(buffer, buffer_size, "Tracker");
            return;
        }
        if (strcmp(result->appearance, "Thermo") == 0 ||
            strcmp(result->appearance, "Heart") == 0 ||
            strcmp(result->appearance, "Sensor") == 0 ||
            strcmp(result->appearance, "Scale") == 0 ||
            strcmp(result->appearance, "Fitness") == 0) {
            ble_manager_copy_text(buffer, buffer_size, "Sensor");
            return;
        }
        if (strcmp(result->appearance, "Phone") == 0 ||
            strcmp(result->appearance, "Computer") == 0 ||
            strcmp(result->appearance, "Watch") == 0 ||
            strcmp(result->appearance, "Clock") == 0 ||
            strcmp(result->appearance, "Eyewear") == 0) {
            ble_manager_copy_text(buffer, buffer_size, "Personal");
            return;
        }
        if (strcmp(result->appearance, "Light") == 0) {
            ble_manager_copy_text(buffer, buffer_size, "Lighting");
            return;
        }
        if (strcmp(result->appearance, "Network") == 0) {
            ble_manager_copy_text(buffer, buffer_size, "Network");
            return;
        }
    }

    for (index = 0; index < summary->uuid16_count; ++index) {
        switch (summary->uuid16s[index]) {
        case 0x1812:
            ble_manager_copy_text(buffer, buffer_size, "Input");
            return;
        case 0x1809:
        case 0x180D:
        case 0x181A:
        case 0x1816:
        case 0x1808:
        case 0x1818:
        case 0x181B:
        case 0x181C:
        case 0x181F:
        case 0x1826:
            ble_manager_copy_text(buffer, buffer_size, "Sensor");
            return;
        case 0x1819:
            ble_manager_copy_text(buffer, buffer_size, "Tracker");
            return;
        }
    }

    if (ble_manager_classify_from_company(summary, buffer, buffer_size)) {
        return;
    }

    ble_manager_copy_text(buffer, buffer_size, result->connectable ? "Accessory" : "Broadcaster");
}

static void ble_manager_proximity_name(
    int8_t rssi,
    int8_t tx_power,
    bool has_tx_power,
    char *buffer,
    size_t buffer_size)
{
    int path_loss;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (has_tx_power) {
        path_loss = tx_power - rssi;
        if (path_loss <= 50) {
            ble_manager_copy_text(buffer, buffer_size, "Close");
        } else if (path_loss <= 68) {
            ble_manager_copy_text(buffer, buffer_size, "Near");
        } else if (path_loss <= 85) {
            ble_manager_copy_text(buffer, buffer_size, "Room");
        } else {
            ble_manager_copy_text(buffer, buffer_size, "Far");
        }
        return;
    }

    if (rssi >= -55) {
        ble_manager_copy_text(buffer, buffer_size, "Close");
    } else if (rssi >= -67) {
        ble_manager_copy_text(buffer, buffer_size, "Near");
    } else if (rssi >= -78) {
        ble_manager_copy_text(buffer, buffer_size, "Room");
    } else {
        ble_manager_copy_text(buffer, buffer_size, "Far");
    }
}

static void ble_manager_parse_adv_summary(
    ble_scan_result_t *result,
    const uint8_t *data,
    uint8_t data_len)
{
    ble_adv_summary_t summary = {0};
    uint8_t offset = 0;

    if (result == NULL || data == NULL || data_len == 0) {
        return;
    }

    while (offset < data_len) {
        uint8_t field_len = data[offset];
        uint8_t payload_len;
        uint8_t type;
        const uint8_t *payload;
        uint8_t index;

        if (field_len == 0) {
            break;
        }

        if ((uint16_t)offset + field_len + 1U > data_len) {
            break;
        }

        type = data[offset + 1U];
        payload = &data[offset + 2U];
        payload_len = field_len - 1U;

        switch (type) {
        case BLE_AD_TYPE_NAME_SHORT:
        case BLE_AD_TYPE_NAME_COMPLETE:
            if (payload_len > 0 && (!result->has_name || type == BLE_AD_TYPE_NAME_COMPLETE)) {
                ble_manager_copy_name(result->name, sizeof(result->name), payload, payload_len);
                result->has_name = true;
            }
            break;
        case BLE_AD_TYPE_TX_POWER:
            if (payload_len >= 1U) {
                summary.tx_power = (int8_t)payload[0];
                summary.has_tx_power = true;
            }
            break;
        case BLE_AD_TYPE_APPEARANCE:
            if (payload_len >= 2U) {
                summary.appearance = ble_manager_read_u16_le(payload);
                summary.has_appearance = true;
            }
            break;
        case BLE_AD_TYPE_MANUFACTURER:
            if (payload_len >= 2U) {
                summary.company_id = ble_manager_read_u16_le(payload);
                summary.has_company = true;
                if (summary.company_id == 0x004C && payload_len >= 4U &&
                    payload[2] == 0x02U && payload[3] == 0x15U) {
                    summary.is_ibeacon = true;
                }
            }
            break;
        case BLE_AD_TYPE_SERVICE_DATA16:
            if (payload_len >= 2U && ble_manager_read_u16_le(payload) == 0xFEAAU) {
                summary.is_eddystone = true;
            }
            break;
        case BLE_AD_TYPE_UUID16_INCOMPLETE:
        case BLE_AD_TYPE_UUID16_COMPLETE:
            for (index = 0; index + 1U < payload_len; index += 2U) {
                ble_manager_append_uuid16(&summary, ble_manager_read_u16_le(&payload[index]));
            }
            break;
        }

        offset = (uint8_t)(offset + field_len + 1U);
    }

    if (summary.has_company) {
        ble_manager_company_name(summary.company_id, result->company, sizeof(result->company));
        result->has_company = true;
    }

    if (summary.has_appearance) {
        ble_manager_appearance_name(
            summary.appearance,
            result->appearance,
            sizeof(result->appearance));
        result->has_appearance = true;
    }

    if (summary.has_tx_power) {
        result->tx_power = summary.tx_power;
        result->has_tx_power = true;
    }

    ble_manager_classification_name(
        result,
        &summary,
        result->classification,
        sizeof(result->classification));
    ble_manager_proximity_name(
        result->rssi,
        result->tx_power,
        result->has_tx_power,
        result->proximity,
        sizeof(result->proximity));
}

static void ble_manager_format_mac(const ble_addr_t *address, char *buffer, size_t buffer_size)
{
    if (address == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    snprintf(
        buffer,
        buffer_size,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        address->val[5],
        address->val[4],
        address->val[3],
        address->val[2],
        address->val[1],
        address->val[0]);
}

static int ble_manager_find_result(const ble_scan_session_t *session, const ble_addr_t *address)
{
    if (session == NULL || address == NULL) {
        return -1;
    }

    for (size_t index = 0; index < BLE_MANAGER_MAX_RESULTS; ++index) {
        if (!session->results[index].in_use) {
            continue;
        }

        if (session->results[index].address.type == address->type &&
            memcmp(session->results[index].address.val, address->val, sizeof(address->val)) == 0) {
            return (int)index;
        }
    }

    return -1;
}

static int ble_manager_reserve_result(ble_scan_session_t *session, const ble_addr_t *address)
{
    if (session == NULL || address == NULL) {
        return -1;
    }

    for (size_t index = 0; index < BLE_MANAGER_MAX_RESULTS; ++index) {
        if (session->results[index].in_use) {
            continue;
        }

        session->results[index].address = *address;
        session->results[index].in_use = true;
        session->results[index].rssi = -127;
        ++session->total_count;
        return (int)index;
    }

    ++session->total_count;
    return -1;
}

static void ble_manager_on_reset(int reason)
{
    (void)reason;
    s_ble_synced = false;
}

static void ble_manager_on_sync(void)
{
    s_ble_synced = true;

    if (s_ble_sync_semaphore != NULL) {
        xSemaphoreGive(s_ble_sync_semaphore);
    }
}

static void ble_manager_host_task(void *parameter)
{
    (void)parameter;

    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t ble_manager_init(void)
{
    esp_err_t err;

    if (s_ble_initialized) {
        return ESP_OK;
    }

    if (s_ble_sync_semaphore == NULL) {
        s_ble_sync_semaphore = xSemaphoreCreateBinary();
    }
    if (s_ble_scan_mutex == NULL) {
        s_ble_scan_mutex = xSemaphoreCreateMutex();
    }
    if (s_ble_sync_semaphore == NULL || s_ble_scan_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }

    ble_hs_cfg.reset_cb = ble_manager_on_reset;
    ble_hs_cfg.sync_cb = ble_manager_on_sync;

    while (xSemaphoreTake(s_ble_sync_semaphore, 0) == pdTRUE) {
    }

    nimble_port_freertos_init(ble_manager_host_task);

    if (!s_ble_synced &&
        xSemaphoreTake(s_ble_sync_semaphore, pdMS_TO_TICKS(BLE_MANAGER_INIT_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_ble_initialized = true;
    return ESP_OK;
}

uint16_t ble_manager_default_scan_duration_ms(void)
{
    return BLE_MANAGER_DEFAULT_SCAN_DURATION_MS;
}

uint16_t ble_manager_clamp_scan_duration_ms(uint32_t duration_ms)
{
    if (duration_ms == 0U) {
        return BLE_MANAGER_DEFAULT_SCAN_DURATION_MS;
    }

    if (duration_ms < BLE_MANAGER_MIN_SCAN_DURATION_MS) {
        return BLE_MANAGER_MIN_SCAN_DURATION_MS;
    }

    if (duration_ms > BLE_MANAGER_MAX_SCAN_DURATION_MS) {
        return BLE_MANAGER_MAX_SCAN_DURATION_MS;
    }

    return (uint16_t)duration_ms;
}

static int ble_manager_gap_event(struct ble_gap_event *event, void *arg)
{
    ble_scan_session_t *session = (ble_scan_session_t *)arg;

    if (session == NULL) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
    {
        int result_index = ble_manager_find_result(session, &event->disc.addr);

        if (result_index < 0) {
            result_index = ble_manager_reserve_result(session, &event->disc.addr);
        }

        if (result_index < 0) {
            return 0;
        }

        ble_scan_result_t *result = &session->results[result_index];
        result->connectable = ble_manager_event_is_connectable(event->disc.event_type);
        if (event->disc.rssi > result->rssi) {
            result->rssi = event->disc.rssi;
        }

        ble_manager_parse_adv_summary(result, event->disc.data, event->disc.length_data);

        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        session->completion_reason = event->disc_complete.reason;
        xSemaphoreGive(session->done_semaphore);
        return 0;
    default:
        return 0;
    }
}

esp_err_t ble_manager_scan(uint16_t duration_ms, ble_scan_result_writer_t write_line, void *context)
{
    ble_scan_session_t session = {0};
    struct ble_gap_disc_params scan_params = {0};
    uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
    const uint16_t clamped_duration_ms = ble_manager_clamp_scan_duration_ms(duration_ms);
    const uint32_t scan_wait_ms = (uint32_t)clamped_duration_ms + 2500U;
    esp_err_t err;
    int rc;

    if (write_line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = ble_manager_init();
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(s_ble_scan_mutex, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    session.done_semaphore = xSemaphoreCreateBinary();
    if (session.done_semaphore == NULL) {
        xSemaphoreGive(s_ble_scan_mutex);
        return ESP_ERR_NO_MEM;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        vSemaphoreDelete(session.done_semaphore);
        xSemaphoreGive(s_ble_scan_mutex);
        return ESP_FAIL;
    }

    scan_params.passive = 1;
    scan_params.filter_duplicates = 1;

    rc = ble_gap_disc(
        own_addr_type,
        clamped_duration_ms,
        &scan_params,
        ble_manager_gap_event,
        &session);
    if (rc != 0) {
        vSemaphoreDelete(session.done_semaphore);
        xSemaphoreGive(s_ble_scan_mutex);
        return ESP_FAIL;
    }

    if (xSemaphoreTake(session.done_semaphore, pdMS_TO_TICKS(scan_wait_ms)) != pdTRUE) {
        ble_gap_disc_cancel();
        vSemaphoreDelete(session.done_semaphore);
        xSemaphoreGive(s_ble_scan_mutex);
        return ESP_ERR_TIMEOUT;
    }

    if (session.completion_reason != 0) {
        vSemaphoreDelete(session.done_semaphore);
        xSemaphoreGive(s_ble_scan_mutex);
        return ESP_FAIL;
    }

    char line[BLE_MANAGER_LINE_SIZE];
    snprintf(line, sizeof(line), "BLE_SCAN_COUNT %u\n", (unsigned)session.total_count);
    write_line(line, context);

    for (size_t index = 0; index < BLE_MANAGER_MAX_RESULTS; ++index) {
        if (!session.results[index].in_use) {
            continue;
        }

        char mac[18];
        ble_manager_format_mac(&session.results[index].address, mac, sizeof(mac));
        snprintf(
            line,
            sizeof(line),
            "BLE_DEVICE %s RSSI %d COMPANY %s APPEAR %s CLASS %s PROX %s ADDR %s CONN %s NAME %s\n",
            mac,
            session.results[index].rssi,
            session.results[index].has_company ? session.results[index].company : "-",
            session.results[index].has_appearance ? session.results[index].appearance : "-",
            session.results[index].classification[0] ? session.results[index].classification : "BLE",
            session.results[index].proximity[0] ? session.results[index].proximity : "-",
            ble_manager_addr_type_to_string(session.results[index].address.type),
            session.results[index].connectable ? "YES" : "NO",
            session.results[index].has_name ? session.results[index].name : "-");
        write_line(line, context);
    }

    if (session.total_count > BLE_MANAGER_MAX_RESULTS) {
        snprintf(
            line,
            sizeof(line),
            "BLE_SCAN_TRUNCATED %u\n",
            (unsigned)(session.total_count - BLE_MANAGER_MAX_RESULTS));
        write_line(line, context);
    }

    write_line("BLE_SCAN_DONE\n", context);

    vSemaphoreDelete(session.done_semaphore);
    xSemaphoreGive(s_ble_scan_mutex);
    return ESP_OK;
}