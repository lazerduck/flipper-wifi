#include "modules/ble/ble_gatt.h"
#include "modules/ble/ble_manager.h"
#include "modules/status_led/status_led.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "nimble/hci_common.h"

/* ---- limits ---- */

#define BLE_GATT_MAX_SVCS           12
#define BLE_GATT_MAX_CHRS_PER_SVC    8
#define BLE_GATT_MAX_READS          (BLE_GATT_MAX_SVCS * BLE_GATT_MAX_CHRS_PER_SVC)
#define BLE_GATT_UUID_STR_SIZE      10   /* "0000180A\0" or "180A\0" */
#define BLE_GATT_NAME_STR_SIZE      16
#define BLE_GATT_PROPS_STR_SIZE     48   /* "READ WRITE NOTIFY INDICATE WRITE_NR" */
#define BLE_GATT_VALUE_STR_SIZE     36
#define BLE_GATT_RAW_HEX_STR_SIZE   (BLE_GATT_VALUE_BUF_CAP * 2 + 1)
#define BLE_GATT_VALUE_BUF_CAP      32
#define BLE_GATT_LINE_SIZE         128
#define BLE_GATT_CONNECT_TIMEOUT_MS 5000
#define BLE_GATT_TOTAL_TIMEOUT_MS  25000

/* ---- UUID lookup tables ---- */

typedef struct {
    uint16_t uuid16;
    const char *name;
} ble_gatt_uuid_name_t;

static const ble_gatt_uuid_name_t s_svc_names[] = {
    {0x1800, "GenAccess"},
    {0x1801, "GenAttr"},
    {0x180A, "DevInfo"},
    {0x180D, "HeartRate"},
    {0x180F, "Battery"},
    {0x1810, "BloodPressure"},
    {0x1812, "HID"},
    {0x1816, "CycleSpeed"},
    {0x181A, "EnvSensing"},
    {0x181C, "UserData"},
    {0x181D, "BodyComp"},
    {0x181E, "WeightScale"},
    {0x182A, "Insulin"},
    {0x1802, "ImmAlert"},
    {0x1803, "LinkLoss"},
    {0x1804, "TxPower"},
    {0x1805, "CurrTime"},
    {0x1808, "Glucose"},
    {0x180E, "PhoneAlert"},
    {0xFE95, "Xiaomi"},
    {0xFD6F, "ExposureNotify"},
    {0xFEAA, "Eddystone"},
    {0xFFF0, "Custom-FFF0"},
    {0xFFE0, "Custom-FFE0"},
};

static const ble_gatt_uuid_name_t s_chr_names[] = {
    {0x2A00, "DevName"},
    {0x2A01, "Appear"},
    {0x2A02, "PrivacyFlag"},
    {0x2A04, "ConnParams"},
    {0x2A05, "SvcChanged"},
    {0x2A19, "BattLvl"},
    {0x2A1C, "TempMeasure"},
    {0x2A1E, "TempType"},
    {0x2A22, "BootKbIn"},
    {0x2A23, "SystemID"},
    {0x2A24, "ModelNum"},
    {0x2A25, "SerialNum"},
    {0x2A26, "FWRev"},
    {0x2A27, "HWRev"},
    {0x2A28, "SWRev"},
    {0x2A29, "Mfr"},
    {0x2A2A, "RegCert"},
    {0x2A37, "HRMeasure"},
    {0x2A38, "HRBodyLoc"},
    {0x2A39, "HRCtrlPt"},
    {0x2A3F, "AlertStatus"},
    {0x2A4A, "HIDInfo"},
    {0x2A4B, "HIDReportMap"},
    {0x2A4C, "HIDCtrlPt"},
    {0x2A4D, "HIDReport"},
    {0x2A50, "PnPID"},
    {0x2A56, "DigitalIO"},
    {0x2A6D, "Pressure"},
    {0x2A6E, "Temp"},
    {0x2A6F, "Humidity"},
    {0x2A77, "Irradiance"},
    {0x2AEB, "ElecEnergy"},
};

/* ---- string characteristic UUIDs (for value decoding) ---- */

static const uint16_t s_string_chr_uuids[] = {
    0x2A00, /* Device Name */
    0x2A24, /* Model Number */
    0x2A25, /* Serial Number */
    0x2A26, /* Firmware Revision */
    0x2A27, /* Hardware Revision */
    0x2A28, /* Software Revision */
    0x2A29, /* Manufacturer Name */
};

/* ---- internal types ---- */

typedef struct {
    char uuid[BLE_GATT_UUID_STR_SIZE];
    char name[BLE_GATT_NAME_STR_SIZE];
    char props[BLE_GATT_PROPS_STR_SIZE];
    char value[BLE_GATT_VALUE_STR_SIZE];
    char raw_hex[BLE_GATT_RAW_HEX_STR_SIZE];
    uint16_t val_handle;
    uint16_t uuid16;        /* 0 for 128-bit UUIDs */
    uint8_t properties;
    bool has_value;
    bool in_use;
} ble_gatt_chr_t;

typedef struct {
    char uuid[BLE_GATT_UUID_STR_SIZE];
    char name[BLE_GATT_NAME_STR_SIZE];
    uint16_t start_handle;
    uint16_t end_handle;
    ble_gatt_chr_t chrs[BLE_GATT_MAX_CHRS_PER_SVC];
    uint8_t chr_count;
    bool in_use;
} ble_gatt_svc_t;

typedef enum {
    BLE_GATT_PHASE_CONNECTING,
    BLE_GATT_PHASE_DISC_SVCS,
    BLE_GATT_PHASE_DISC_CHRS,
    BLE_GATT_PHASE_READING,
    BLE_GATT_PHASE_DONE,
    BLE_GATT_PHASE_FAILED,
} ble_gatt_phase_t;

typedef enum {
    BLE_GATT_FAIL_NONE,
    BLE_GATT_FAIL_CONNECT,
    BLE_GATT_FAIL_SVC_DISC,
    BLE_GATT_FAIL_CHR_DISC,
    BLE_GATT_FAIL_DISCONNECT,
    BLE_GATT_FAIL_TIMEOUT,
} ble_gatt_fail_stage_t;

typedef struct {
    ble_addr_t peer_addr;
    uint16_t conn_handle;
    ble_gatt_svc_t svcs[BLE_GATT_MAX_SVCS];
    uint8_t svc_count;
    uint8_t cur_svc_idx;
    /* read queue: parallel arrays of (svc_idx, chr_idx) */
    uint8_t read_queue_svc[BLE_GATT_MAX_READS];
    uint8_t read_queue_chr[BLE_GATT_MAX_READS];
    uint8_t read_queue_len;
    uint8_t read_pos;
    SemaphoreHandle_t done_semaphore;
    ble_gatt_phase_t phase;
    ble_gatt_fail_stage_t fail_stage;
    int fail_status;
    bool semaphore_given;
} ble_gatt_session_t;

/* Static session buffer keeps this large state off the caller stack. */
static ble_gatt_session_t s_ble_gatt_session;

/* ---- helpers ---- */

static const char *ble_gatt_lookup_svc_name(uint16_t uuid16)
{
    for (size_t i = 0; i < sizeof(s_svc_names) / sizeof(s_svc_names[0]); i++) {
        if (s_svc_names[i].uuid16 == uuid16) {
            return s_svc_names[i].name;
        }
    }
    return NULL;
}

static const char *ble_gatt_lookup_chr_name(uint16_t uuid16)
{
    for (size_t i = 0; i < sizeof(s_chr_names) / sizeof(s_chr_names[0]); i++) {
        if (s_chr_names[i].uuid16 == uuid16) {
            return s_chr_names[i].name;
        }
    }
    return NULL;
}

static bool ble_gatt_is_string_chr(uint16_t uuid16)
{
    for (size_t i = 0; i < sizeof(s_string_chr_uuids) / sizeof(s_string_chr_uuids[0]); i++) {
        if (s_string_chr_uuids[i] == uuid16) {
            return true;
        }
    }
    return false;
}

static void ble_gatt_uuid_to_str(const ble_uuid_t *uuid, char *buf, size_t size)
{
    if (uuid->type == BLE_UUID_TYPE_16) {
        snprintf(buf, size, "%04X", (unsigned)BLE_UUID16(uuid)->value);
    } else if (uuid->type == BLE_UUID_TYPE_128) {
        /* NimBLE stores 128-bit UUIDs little-endian; bytes 15..12 are MSB */
        const uint8_t *v = BLE_UUID128(uuid)->value;
        snprintf(buf, size, "%02X%02X%02X%02X", v[15], v[14], v[13], v[12]);
    } else {
        snprintf(buf, size, "????");
    }
}

static uint16_t ble_gatt_uuid_to_u16(const ble_uuid_t *uuid)
{
    if (uuid->type == BLE_UUID_TYPE_16) {
        return BLE_UUID16(uuid)->value;
    }
    return 0;
}

static void ble_gatt_props_to_str(uint8_t properties, char *buf, size_t size)
{
    size_t pos = 0;
    buf[0] = '\0';

    static const struct { uint8_t bit; const char *name; } flags[] = {
        {BLE_GATT_CHR_PROP_READ,        "READ"},
        {BLE_GATT_CHR_PROP_WRITE,       "WRITE"},
        {BLE_GATT_CHR_PROP_NOTIFY,      "NOTIFY"},
        {BLE_GATT_CHR_PROP_INDICATE,    "INDICATE"},
        {BLE_GATT_CHR_PROP_WRITE_NO_RSP,"WRITE_NR"},
        {BLE_GATT_CHR_PROP_BROADCAST,   "BCAST"},
    };

    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        if (!(properties & flags[i].bit)) {
            continue;
        }
        if (pos > 0 && pos + 1 < size) {
            buf[pos++] = ' ';
            buf[pos] = '\0';
        }
        size_t name_len = strlen(flags[i].name);
        if (pos + name_len < size) {
            memcpy(buf + pos, flags[i].name, name_len);
            pos += name_len;
            buf[pos] = '\0';
        }
    }

    if (pos == 0) {
        snprintf(buf, size, "NONE");
    }
}

static bool ble_gatt_try_decode_ascii(
    const uint8_t *buf,
    uint16_t len,
    char *out,
    size_t out_size)
{
    uint16_t trimmed_len = len;
    uint16_t printable = 0;

    while (trimmed_len > 0 && buf[trimmed_len - 1] == 0x00) {
        trimmed_len--;
    }

    if (trimmed_len < 4 || out_size < 2) {
        return false;
    }

    for (uint16_t i = 0; i < trimmed_len; i++) {
        const uint8_t ch = buf[i];
        if ((ch >= 0x20 && ch < 0x7F) || ch == '\t') {
            printable++;
        }
    }

    if (((uint32_t)printable * 100U) / (uint32_t)trimmed_len < 85U) {
        return false;
    }

    size_t copy_len = trimmed_len;
    if (copy_len >= out_size) {
        copy_len = out_size - 1U;
    }

    for (size_t i = 0; i < copy_len; i++) {
        const uint8_t ch = buf[i];
        out[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : ' ';
    }
    out[copy_len] = '\0';
    return true;
}

static bool ble_gatt_try_decode_utf16le(
    const uint8_t *buf,
    uint16_t len,
    char *out,
    size_t out_size)
{
    uint16_t trimmed_len = len;
    uint16_t pairs = 0;
    uint16_t printable = 0;

    while (trimmed_len >= 2 && buf[trimmed_len - 1] == 0x00 && buf[trimmed_len - 2] == 0x00) {
        trimmed_len -= 2;
    }

    if ((trimmed_len < 4) || ((trimmed_len & 1U) != 0) || out_size < 2) {
        return false;
    }

    pairs = trimmed_len / 2U;
    for (uint16_t i = 0; i < pairs; i++) {
        const uint8_t lo = buf[i * 2U];
        const uint8_t hi = buf[i * 2U + 1U];

        if (hi != 0x00) {
            return false;
        }

        if ((lo >= 0x20 && lo < 0x7F) || lo == '\t') {
            printable++;
        }
    }

    if (((uint32_t)printable * 100U) / (uint32_t)pairs < 85U) {
        return false;
    }

    size_t copy_len = pairs;
    if (copy_len >= out_size) {
        copy_len = out_size - 1U;
    }

    for (size_t i = 0; i < copy_len; i++) {
        const uint8_t lo = buf[i * 2U];
        out[i] = (lo >= 0x20 && lo < 0x7F) ? (char)lo : ' ';
    }

    out[copy_len] = '\0';
    return true;
}

static void ble_gatt_hex_encode(
    const uint8_t *buf,
    uint16_t len,
    char *out,
    size_t out_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;

    if (out_size == 0) {
        return;
    }

    out[0] = '\0';

    for (uint16_t i = 0; i < len && pos + 2 < out_size; i++) {
        out[pos++] = hex[(buf[i] >> 4) & 0x0F];
        out[pos++] = hex[buf[i] & 0x0F];
    }
    out[pos] = '\0';
}

static void ble_gatt_decode_value(ble_gatt_chr_t *chr, struct os_mbuf *om)
{
    uint8_t buf[BLE_GATT_VALUE_BUF_CAP];
    uint16_t len = 0;

    if (ble_hs_mbuf_to_flat(om, buf, sizeof(buf), &len) != 0 || len == 0) {
        return;
    }

    ble_gatt_hex_encode(buf, len, chr->raw_hex, sizeof(chr->raw_hex));

    chr->has_value = true;

    /* Battery Level: uint8 → "87%" */
    if (chr->uuid16 == 0x2A19 && len >= 1) {
        snprintf(chr->value, sizeof(chr->value), "%u%%", (unsigned)buf[0]);
        return;
    }

    /* Appearance: uint16 */
    if (chr->uuid16 == 0x2A01 && len >= 2) {
        const uint16_t appearance = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        snprintf(chr->value, sizeof(chr->value), "appearance 0x%04X", appearance);
        return;
    }

    /* Peripheral Preferred Connection Parameters */
    if (chr->uuid16 == 0x2A04 && len >= 8) {
        const uint16_t min_itvl = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        const uint16_t max_itvl = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
        const uint16_t latency = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
        const uint16_t timeout = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);
        const uint32_t min_ms_x10 = (uint32_t)min_itvl * 125U / 10U;
        const uint32_t max_ms_x10 = (uint32_t)max_itvl * 125U / 10U;
        snprintf(
            chr->value,
            sizeof(chr->value),
            "i %u.%u-%u.%u l%u t%u",
            (unsigned)(min_ms_x10 / 10U),
            (unsigned)(min_ms_x10 % 10U),
            (unsigned)(max_ms_x10 / 10U),
            (unsigned)(max_ms_x10 % 10U),
            (unsigned)latency,
            (unsigned)timeout * 10U);
        return;
    }

    /* Service Changed: start_handle + end_handle */
    if (chr->uuid16 == 0x2A05 && len >= 4) {
        const uint16_t start_h = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        const uint16_t end_h = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
        snprintf(chr->value, sizeof(chr->value), "chg 0x%04X-0x%04X", start_h, end_h);
        return;
    }

    /* System ID */
    if (chr->uuid16 == 0x2A23 && len >= 8) {
        snprintf(
            chr->value,
            sizeof(chr->value),
            "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
            buf[0],
            buf[1],
            buf[2],
            buf[3],
            buf[4],
            buf[5],
            buf[6],
            buf[7]);
        return;
    }

    /* PnP ID: vendor src + vendor id + product id + product version */
    if (chr->uuid16 == 0x2A50 && len >= 7) {
        const uint8_t src = buf[0];
        const uint16_t vid = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
        const uint16_t pid = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
        const uint16_t ver = (uint16_t)buf[5] | ((uint16_t)buf[6] << 8);
        snprintf(
            chr->value,
            sizeof(chr->value),
            "src%u vid%04X pid%04X v%04X",
            (unsigned)src,
            vid,
            pid,
            ver);
        return;
    }

    /* Known string characteristics */
    if (ble_gatt_is_string_chr(chr->uuid16)) {
        if (ble_gatt_try_decode_ascii(buf, len, chr->value, sizeof(chr->value))) {
            return;
        }

        if (ble_gatt_try_decode_utf16le(buf, len, chr->value, sizeof(chr->value))) {
            return;
        }

        size_t copy_len = (len < sizeof(chr->value) - 1U) ? len : sizeof(chr->value) - 1U;
        for (size_t i = 0; i < copy_len; i++) {
            chr->value[i] = (buf[i] >= 0x20 && buf[i] < 0x7F) ? (char)buf[i] : '.';
        }
        chr->value[copy_len] = '\0';
        return;
    }

    /* Generic fallback: if it looks like text, show text instead of hex bytes. */
    if (ble_gatt_try_decode_ascii(buf, len, chr->value, sizeof(chr->value))) {
        return;
    }

    if (ble_gatt_try_decode_utf16le(buf, len, chr->value, sizeof(chr->value))) {
        return;
    }

    /* Generic: hex dump up to 8 bytes */
    size_t dump_len = (len > 8U) ? 8U : len;
    size_t pos = 0;
    for (size_t i = 0; i < dump_len; i++) {
        if (i > 0 && pos + 1 < sizeof(chr->value)) {
            chr->value[pos++] = ' ';
        }
        if (pos + 2 < sizeof(chr->value)) {
            snprintf(chr->value + pos, sizeof(chr->value) - pos, "%02X", buf[i]);
            pos += 2;
        }
    }
    if (len > 8U && pos + 3 < sizeof(chr->value)) {
        memcpy(chr->value + pos, "...", 3);
        pos += 3;
    }
    chr->value[pos] = '\0';
}

static bool ble_gatt_parse_mac(const char *mac_str, uint8_t *addr_out)
{
    unsigned int b[6];
    if (sscanf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    /* NimBLE stores MAC bytes in reverse order */
    addr_out[0] = (uint8_t)b[5];
    addr_out[1] = (uint8_t)b[4];
    addr_out[2] = (uint8_t)b[3];
    addr_out[3] = (uint8_t)b[2];
    addr_out[4] = (uint8_t)b[1];
    addr_out[5] = (uint8_t)b[0];
    return true;
}

static uint8_t ble_gatt_parse_addr_type(const char *type_str)
{
    if (type_str == NULL) {
        return BLE_ADDR_RANDOM;
    }
    if (strcmp(type_str, "PUBLIC") == 0) {
        return BLE_ADDR_PUBLIC;
    }
    if (strcmp(type_str, "PUBLIC_ID") == 0) {
        return BLE_ADDR_PUBLIC_ID;
    }
    if (strcmp(type_str, "RANDOM_ID") == 0) {
        return BLE_ADDR_RANDOM_ID;
    }
    return BLE_ADDR_RANDOM;
}

static void ble_gatt_signal_done(ble_gatt_session_t *session)
{
    if (!session->semaphore_given) {
        session->semaphore_given = true;
        xSemaphoreGive(session->done_semaphore);
    }
}

static const char *ble_gatt_fail_stage_to_string(ble_gatt_fail_stage_t stage)
{
    switch (stage) {
    case BLE_GATT_FAIL_CONNECT:
        return "connect";
    case BLE_GATT_FAIL_SVC_DISC:
        return "svc";
    case BLE_GATT_FAIL_CHR_DISC:
        return "chr";
    case BLE_GATT_FAIL_DISCONNECT:
        return "disconnect";
    case BLE_GATT_FAIL_TIMEOUT:
        return "timeout";
    case BLE_GATT_FAIL_NONE:
    default:
        return "unknown";
    }
}

static const char *ble_gatt_status_to_name(int status)
{
    /* ATT errors are often encoded as 0x100 + ATT code by NimBLE host APIs. */
    int att_status = status;
    if ((status & 0xFF00) == 0x0100) {
        att_status = status & 0x00FF;
    }

    switch (att_status) {
    case 0x02:
        return "read_not_permitted";
    case 0x05:
        return "auth_required";
    case 0x08:
        return "authorization_required";
    case 0x0A:
        return "attr_not_found";
    case 0x0C:
        return "encryption_key_size";
    case 0x0D:
        return "invalid_attr_value_len";
    case 0x0E:
        return "unlikely_error";
    case 0x0F:
        return "encryption_required";
    default:
        break;
    }

    switch (status) {
    case BLE_HS_ENOTCONN:
        return "not_connected";
    case BLE_HS_ETIMEOUT:
        return "timeout";
    case BLE_HS_EBUSY:
        return "busy";
    default:
        return "unknown";
    }
}

static void ble_gatt_terminate(ble_gatt_session_t *session)
{
    ble_gap_terminate(session->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    /* semaphore is signaled from the resulting BLE_GAP_EVENT_DISCONNECT */
}

/* ---- forward declarations ---- */

static int ble_gatt_read_cb(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr,
    void *arg);

/* ---- read queue ---- */

static void ble_gatt_build_read_queue(ble_gatt_session_t *session)
{
    session->read_queue_len = 0;
    session->read_pos = 0;

    for (uint8_t si = 0; si < session->svc_count; si++) {
        const ble_gatt_svc_t *svc = &session->svcs[si];
        for (uint8_t ci = 0; ci < svc->chr_count; ci++) {
            if ((session->svcs[si].chrs[ci].props[0] != '\0') &&
                (strstr(session->svcs[si].chrs[ci].props, "READ") != NULL) &&
                session->read_queue_len < BLE_GATT_MAX_READS) {
                session->read_queue_svc[session->read_queue_len] = si;
                session->read_queue_chr[session->read_queue_len] = ci;
                session->read_queue_len++;
            }
        }
    }
}

static void ble_gatt_start_next_read(ble_gatt_session_t *session)
{
    if (session->read_pos >= session->read_queue_len) {
        session->phase = BLE_GATT_PHASE_DONE;
        ble_gatt_terminate(session);
        return;
    }

    uint8_t si = session->read_queue_svc[session->read_pos];
    uint8_t ci = session->read_queue_chr[session->read_pos];
    ble_gatt_chr_t *chr = &session->svcs[si].chrs[ci];
    uint16_t val_handle = chr->val_handle;

    int rc = ble_gattc_read(session->conn_handle, val_handle, ble_gatt_read_cb, session);
    if (rc != 0) {
        chr->has_value = true;
        chr->raw_hex[0] = '\0';
        snprintf(
            chr->value,
            sizeof(chr->value),
            "read_err %d %s",
            rc,
            ble_gatt_status_to_name(rc));
        /* Skip this chr on error */
        session->read_pos++;
        ble_gatt_start_next_read(session);
    }
}

/* ---- callbacks (all called from NimBLE task) ---- */

static int ble_gatt_read_cb(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr,
    void *arg)
{
    ble_gatt_session_t *session = (ble_gatt_session_t *)arg;

    (void)conn_handle;

    if (session == NULL) {
        return 0;
    }

    if (error->status == 0 && attr != NULL && attr->om != NULL) {
        uint8_t si = session->read_queue_svc[session->read_pos];
        uint8_t ci = session->read_queue_chr[session->read_pos];
        ble_gatt_decode_value(&session->svcs[si].chrs[ci], attr->om);
    } else if (error->status != 0) {
        uint8_t si = session->read_queue_svc[session->read_pos];
        uint8_t ci = session->read_queue_chr[session->read_pos];
        ble_gatt_chr_t *chr = &session->svcs[si].chrs[ci];
        chr->has_value = true;
        chr->raw_hex[0] = '\0';
        snprintf(
            chr->value,
            sizeof(chr->value),
            "read_err %d %s",
            error->status,
            ble_gatt_status_to_name(error->status));
    }

    session->read_pos++;
    ble_gatt_start_next_read(session);

    return 0;
}

static int ble_gatt_chr_disc_cb(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    const struct ble_gatt_chr *chr,
    void *arg)
{
    ble_gatt_session_t *session = (ble_gatt_session_t *)arg;

    (void)conn_handle;

    if (session == NULL) {
        return 0;
    }

    ble_gatt_svc_t *svc = &session->svcs[session->cur_svc_idx];

    if (error->status == 0 && chr != NULL) {
        if (svc->chr_count < BLE_GATT_MAX_CHRS_PER_SVC) {
            ble_gatt_chr_t *dst = &svc->chrs[svc->chr_count];
            ble_gatt_uuid_to_str(&chr->uuid.u, dst->uuid, sizeof(dst->uuid));
            dst->uuid16 = ble_gatt_uuid_to_u16(&chr->uuid.u);
            dst->val_handle = chr->val_handle;
            dst->properties = chr->properties;
            ble_gatt_props_to_str(chr->properties, dst->props, sizeof(dst->props));

            const char *name = ble_gatt_lookup_chr_name(dst->uuid16);
            if (name != NULL) {
                snprintf(dst->name, sizeof(dst->name), "%s", name);
            } else {
                dst->name[0] = '-';
                dst->name[1] = '\0';
            }

            dst->in_use = true;
            svc->chr_count++;
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE) {
        /* Move to next service */
        session->cur_svc_idx++;

        if (session->cur_svc_idx < session->svc_count) {
            ble_gatt_svc_t *next_svc = &session->svcs[session->cur_svc_idx];
            int rc = ble_gattc_disc_all_chrs(
                conn_handle,
                next_svc->start_handle,
                next_svc->end_handle,
                ble_gatt_chr_disc_cb,
                session);
            if (rc != 0) {
                session->phase = BLE_GATT_PHASE_FAILED;
                session->fail_stage = BLE_GATT_FAIL_CHR_DISC;
                session->fail_status = rc;
                ble_gatt_terminate(session);
            }
        } else {
            /* All chr discoveries complete — build read queue and start reads */
            ble_gatt_build_read_queue(session);
            if (session->read_queue_len > 0) {
                session->phase = BLE_GATT_PHASE_READING;
                ble_gatt_start_next_read(session);
            } else {
                session->phase = BLE_GATT_PHASE_DONE;
                ble_gatt_terminate(session);
            }
        }
        return 0;
    }

    /* Any other error — fail */
    session->phase = BLE_GATT_PHASE_FAILED;
    session->fail_stage = BLE_GATT_FAIL_CHR_DISC;
    session->fail_status = error->status;
    ble_gatt_terminate(session);
    return 0;
}

static int ble_gatt_svc_disc_cb(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    const struct ble_gatt_svc *svc,
    void *arg)
{
    ble_gatt_session_t *session = (ble_gatt_session_t *)arg;

    if (session == NULL) {
        return 0;
    }

    if (error->status == 0 && svc != NULL) {
        if (session->svc_count < BLE_GATT_MAX_SVCS) {
            ble_gatt_svc_t *dst = &session->svcs[session->svc_count];
            ble_gatt_uuid_to_str(&svc->uuid.u, dst->uuid, sizeof(dst->uuid));
            uint16_t uuid16 = ble_gatt_uuid_to_u16(&svc->uuid.u);

            const char *name = ble_gatt_lookup_svc_name(uuid16);
            if (name != NULL) {
                snprintf(dst->name, sizeof(dst->name), "%s", name);
            } else {
                dst->name[0] = '-';
                dst->name[1] = '\0';
            }

            dst->start_handle = svc->start_handle;
            dst->end_handle = svc->end_handle;
            dst->in_use = true;
            session->svc_count++;
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE) {
        if (session->svc_count == 0) {
            /* No services — clean disconnect */
            session->phase = BLE_GATT_PHASE_DONE;
            ble_gatt_terminate(session);
            return 0;
        }

        /* Start chr discovery for first service */
        session->cur_svc_idx = 0;
        session->phase = BLE_GATT_PHASE_DISC_CHRS;

        int rc = ble_gattc_disc_all_chrs(
            conn_handle,
            session->svcs[0].start_handle,
            session->svcs[0].end_handle,
            ble_gatt_chr_disc_cb,
            session);
        if (rc != 0) {
            session->phase = BLE_GATT_PHASE_FAILED;
            session->fail_stage = BLE_GATT_FAIL_CHR_DISC;
            session->fail_status = rc;
            ble_gatt_terminate(session);
        }
        return 0;
    }

    /* Discovery error */
    session->phase = BLE_GATT_PHASE_FAILED;
    session->fail_stage = BLE_GATT_FAIL_SVC_DISC;
    session->fail_status = error->status;
    ble_gatt_terminate(session);
    return 0;
}

static int ble_gatt_gap_event(struct ble_gap_event *event, void *arg)
{
    ble_gatt_session_t *session = (ble_gatt_session_t *)arg;

    if (session == NULL) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            session->conn_handle = event->connect.conn_handle;
            session->phase = BLE_GATT_PHASE_DISC_SVCS;
            (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_GATT_ACTIVE);

            int rc = ble_gattc_disc_all_svcs(
                session->conn_handle,
                ble_gatt_svc_disc_cb,
                session);
            if (rc != 0) {
                session->phase = BLE_GATT_PHASE_FAILED;
                session->fail_stage = BLE_GATT_FAIL_SVC_DISC;
                session->fail_status = rc;
                ble_gatt_terminate(session);
            }
        } else {
            /* Connection failed — no disconnect event will follow */
            session->phase = BLE_GATT_PHASE_FAILED;
            session->fail_stage = BLE_GATT_FAIL_CONNECT;
            session->fail_status = event->connect.status;
            (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_IDLE);
            ble_gatt_signal_done(session);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        (void)status_led_set_ble_state(STATUS_LED_BLE_STATE_IDLE);
        if (session->phase == BLE_GATT_PHASE_DONE ||
            session->phase == BLE_GATT_PHASE_FAILED) {
            /* Expected disconnect after terminate or connect failure */
        } else {
            /* Unexpected disconnect — treat as failure */
            session->phase = BLE_GATT_PHASE_FAILED;
            session->fail_stage = BLE_GATT_FAIL_DISCONNECT;
            session->fail_status = event->disconnect.reason;
        }
        ble_gatt_signal_done(session);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        const uint16_t attr_handle = event->notify_rx.attr_handle;
        for (uint8_t si = 0; si < session->svc_count; si++) {
            ble_gatt_svc_t *svc = &session->svcs[si];
            for (uint8_t ci = 0; ci < svc->chr_count; ci++) {
                ble_gatt_chr_t *chr = &svc->chrs[ci];
                if (chr->val_handle == attr_handle && event->notify_rx.om != NULL) {
                    ble_gatt_decode_value(chr, event->notify_rx.om);
                    return 0;
                }
            }
        }
        return 0;
    }

    default:
        return 0;
    }
}

/* ---- public API ---- */

esp_err_t ble_gatt_inspect(
    const char *mac_str,
    const char *addr_type_str,
    ble_scan_result_writer_t write_line,
    void *context)
{
    ble_gatt_session_t *session = &s_ble_gatt_session;

    if (mac_str == NULL || write_line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ble_manager_ensure_init();
    if (err != ESP_OK) {
        return err;
    }

    if (!ble_manager_try_acquire()) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(session, 0, sizeof(*session));

    if (!ble_gatt_parse_mac(mac_str, session->peer_addr.val)) {
        ble_manager_release();
        return ESP_ERR_INVALID_ARG;
    }
    session->peer_addr.type = ble_gatt_parse_addr_type(addr_type_str);
    session->conn_handle = BLE_HS_CONN_HANDLE_NONE;
    session->phase = BLE_GATT_PHASE_CONNECTING;
    session->fail_stage = BLE_GATT_FAIL_NONE;
    session->fail_status = 0;

    session->done_semaphore = xSemaphoreCreateBinary();
    if (session->done_semaphore == NULL) {
        ble_manager_release();
        return ESP_ERR_NO_MEM;
    }

    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        vSemaphoreDelete(session->done_semaphore);
        ble_manager_release();
        return ESP_FAIL;
    }

    /* Emit start line immediately */
    char line[BLE_GATT_LINE_SIZE];
    snprintf(line, sizeof(line), "BLE_GATT_START %s\n", mac_str);
    write_line(line, context);

    rc = ble_gap_connect(
        own_addr_type,
        &session->peer_addr,
        BLE_GATT_CONNECT_TIMEOUT_MS,
        NULL,
        ble_gatt_gap_event,
        session);
    if (rc != 0) {
        vSemaphoreDelete(session->done_semaphore);
        ble_manager_release();
        snprintf(line, sizeof(line), "BLE_GATT_CONNECT_FAILED status=%d\n", rc);
        write_line(line, context);
        return ESP_FAIL;
    }

    /* Block until done (with overall timeout) */
    if (xSemaphoreTake(
            session->done_semaphore,
            pdMS_TO_TICKS(BLE_GATT_TOTAL_TIMEOUT_MS)) != pdTRUE) {
        /* Timeout — try to cancel the connection */
        session->phase = BLE_GATT_PHASE_FAILED;
        session->fail_stage = BLE_GATT_FAIL_TIMEOUT;
        session->fail_status = 0;
        if (session->conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(session->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        } else {
            ble_gap_conn_cancel();
        }
        (void)xSemaphoreTake(session->done_semaphore, pdMS_TO_TICKS(1000));
        vSemaphoreDelete(session->done_semaphore);
        ble_manager_release();
        write_line("BLE_GATT_CONNECT_FAILED stage=timeout\n", context);
        return ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(session->done_semaphore);

    if (session->phase == BLE_GATT_PHASE_FAILED) {
        ble_manager_release();
        if (session->fail_stage == BLE_GATT_FAIL_CONNECT ||
            (session->svc_count == 0 && session->conn_handle == BLE_HS_CONN_HANDLE_NONE)) {
            snprintf(
                line,
                sizeof(line),
                "BLE_GATT_CONNECT_FAILED status=%d\n",
                session->fail_status);
            write_line(line, context);
        } else {
            snprintf(
                line,
                sizeof(line),
                "BLE_GATT_DISCOVER_FAILED stage=%s status=%d\n",
                ble_gatt_fail_stage_to_string(session->fail_stage),
                session->fail_status);
            write_line(line, context);
        }
        return ESP_FAIL;
    }

    /* Emit results */
    write_line("BLE_GATT_CONNECTED\n", context);

    for (uint8_t si = 0; si < session->svc_count; si++) {
        const ble_gatt_svc_t *svc = &session->svcs[si];
        snprintf(
            line,
            sizeof(line),
            "BLE_GATT_SVC %s NAME %s\n",
            svc->uuid,
            svc->name);
        write_line(line, context);

        for (uint8_t ci = 0; ci < svc->chr_count; ci++) {
            const ble_gatt_chr_t *chr = &svc->chrs[ci];
            snprintf(
                line,
                sizeof(line),
                "BLE_GATT_CHR %s %s PROPS %s NAME %s\n",
                svc->uuid,
                chr->uuid,
                chr->props,
                chr->name);
            write_line(line, context);

            if (chr->has_value) {
                snprintf(
                    line,
                    sizeof(line),
                    "BLE_GATT_VAL %s %s %s\n",
                    svc->uuid,
                    chr->uuid,
                    chr->value);
                write_line(line, context);

                if (chr->raw_hex[0] != '\0') {
                    snprintf(
                        line,
                        sizeof(line),
                        "BLE_GATT_RAW %s %s %s\n",
                        svc->uuid,
                        chr->uuid,
                        chr->raw_hex);
                    write_line(line, context);
                }
            }
        }
    }

    write_line("BLE_GATT_DONE\n", context);

    ble_manager_release();
    return ESP_OK;
}
