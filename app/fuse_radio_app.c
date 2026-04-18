#include "fuse_radio_app_i.h"

#include <gui/elements.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define FUSE_RADIO_RX_STREAM_SIZE 512U
#define FUSE_RADIO_DATA_DIR APP_DATA_PATH("fuse_radio")
#define FUSE_RADIO_CREDENTIALS_PATH APP_DATA_PATH("fuse_radio/credentials.fff")
#define FUSE_RADIO_CREDENTIALS_FILETYPE "Fuse Radio Credentials"
#define FUSE_RADIO_CREDENTIALS_VERSION 1U

static size_t fuse_radio_app_strlcpy(char* dst, const char* src, size_t size) {
    const size_t length = strlen(src);

    if(size != 0U) {
        size_t copy_length = length;
        if(copy_length >= size) {
            copy_length = size - 1U;
        }

        memcpy(dst, src, copy_length);
        dst[copy_length] = '\0';
    }

    return length;
}

static int32_t fuse_radio_app_find_saved_credential_index(FuseRadioApp* app, const char* ssid) {
    for(uint8_t index = 0U; index < app->saved_credential_count; index++) {
        if(strcmp(app->saved_credentials[index].ssid, ssid) == 0) {
            return (int32_t)index;
        }
    }

    return -1;
}

static bool fuse_radio_app_save_credentials(FuseRadioApp* app) {
    if(!app->storage || !app->credentials_format) {
        return false;
    }

    if(!storage_dir_exists(app->storage, FUSE_RADIO_DATA_DIR) &&
       !storage_simply_mkdir(app->storage, FUSE_RADIO_DATA_DIR)) {
        return false;
    }

    if(!flipper_format_file_open_always(app->credentials_format, FUSE_RADIO_CREDENTIALS_PATH)) {
        return false;
    }

    bool success = true;
    do {
        if(!flipper_format_write_header_cstr(
               app->credentials_format,
               FUSE_RADIO_CREDENTIALS_FILETYPE,
               FUSE_RADIO_CREDENTIALS_VERSION)) {
            success = false;
            break;
        }

        for(uint8_t index = 0U; index < app->saved_credential_count; index++) {
            const FuseRadioSavedCredential* credential = &app->saved_credentials[index];
            if(!flipper_format_write_string_cstr(app->credentials_format, "SSID", credential->ssid) ||
               !flipper_format_write_string_cstr(
                   app->credentials_format, "Password", credential->password)) {
                success = false;
                break;
            }

            flipper_format_write_empty_line(app->credentials_format);
        }
    } while(false);

    flipper_format_file_close(app->credentials_format);
    return success;
}

static void fuse_radio_app_load_credentials(FuseRadioApp* app) {
    app->saved_credential_count = 0U;

    if(!app->storage || !app->credentials_format) {
        return;
    }

    if(!storage_file_exists(app->storage, FUSE_RADIO_CREDENTIALS_PATH)) {
        return;
    }

    if(!flipper_format_file_open_existing(app->credentials_format, FUSE_RADIO_CREDENTIALS_PATH)) {
        return;
    }

    FuriString* filetype = furi_string_alloc();
    FuriString* ssid = furi_string_alloc();
    FuriString* password = furi_string_alloc();
    uint32_t version = 0U;

    do {
        if(!flipper_format_read_header(app->credentials_format, filetype, &version)) {
            break;
        }

        if((strcmp(furi_string_get_cstr(filetype), FUSE_RADIO_CREDENTIALS_FILETYPE) != 0) ||
           (version != FUSE_RADIO_CREDENTIALS_VERSION)) {
            break;
        }

        while(app->saved_credential_count < FUSE_RADIO_MAX_SAVED_NETWORKS) {
            if(!flipper_format_read_string(app->credentials_format, "SSID", ssid)) {
                break;
            }

            if(!flipper_format_read_string(app->credentials_format, "Password", password)) {
                break;
            }

            FuseRadioSavedCredential* credential =
                &app->saved_credentials[app->saved_credential_count++];
            fuse_radio_app_strlcpy(
                credential->ssid, furi_string_get_cstr(ssid), sizeof(credential->ssid));
            fuse_radio_app_strlcpy(
                credential->password,
                furi_string_get_cstr(password),
                sizeof(credential->password));
        }
    } while(false);

    furi_string_free(password);
    furi_string_free(ssid);
    furi_string_free(filetype);
    flipper_format_file_close(app->credentials_format);
}

const char* fuse_radio_app_get_saved_password(FuseRadioApp* app, const char* ssid) {
    const int32_t index = fuse_radio_app_find_saved_credential_index(app, ssid);
    if(index < 0) {
        return NULL;
    }

    return app->saved_credentials[index].password;
}

bool fuse_radio_app_store_saved_credential(FuseRadioApp* app, const char* ssid, const char* password) {
    if(!ssid[0] || !password[0]) {
        return false;
    }

    int32_t index = fuse_radio_app_find_saved_credential_index(app, ssid);
    if(index < 0) {
        if(app->saved_credential_count < FUSE_RADIO_MAX_SAVED_NETWORKS) {
            index = (int32_t)app->saved_credential_count++;
        } else {
            memmove(
                &app->saved_credentials[0],
                &app->saved_credentials[1],
                sizeof(app->saved_credentials[0]) * (FUSE_RADIO_MAX_SAVED_NETWORKS - 1U));
            index = FUSE_RADIO_MAX_SAVED_NETWORKS - 1;
        }
    }

    FuseRadioSavedCredential* credential = &app->saved_credentials[index];
    fuse_radio_app_strlcpy(credential->ssid, ssid, sizeof(credential->ssid));
    fuse_radio_app_strlcpy(credential->password, password, sizeof(credential->password));

    return fuse_radio_app_save_credentials(app);
}

static bool fuse_radio_app_custom_event_callback(void* context, uint32_t event) {
    FuseRadioApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool fuse_radio_app_back_event_callback(void* context) {
    FuseRadioApp* app = context;

    if(scene_manager_handle_back_event(app->scene_manager)) {
        return true;
    }

    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static void fuse_radio_app_scan_view_callback(FuseRadioScanViewAction action, void* context) {
    FuseRadioApp* app = context;

    if(action == FuseRadioScanViewActionRescan) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventScanRescan);
    } else if(action == FuseRadioScanViewActionSelect) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventScanSelect);
    }
}

void fuse_radio_app_text_input_callback(void* context) {
    FuseRadioApp* app = context;

    if(app->text_input_mode == FuseRadioTextInputConnectSsid) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventConnectSsidDone);
    } else if(app->text_input_mode == FuseRadioTextInputConnectPassword) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventConnectPasswordDone);
    } else if(app->text_input_mode == FuseRadioTextInputMdnsHost) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventMdnsHostDone);
    }
}

static void
    fuse_radio_app_status_button_callback(GuiButtonType button, InputType type, void* context) {
    FuseRadioApp* app = context;

    if(button == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventRetryDetection);
    }
}

static void fuse_radio_app_wifi_status_button_callback(
    GuiButtonType button,
    InputType type,
    void* context) {
    FuseRadioApp* app = context;

    if(type != InputTypeShort) {
        return;
    }

    if(button == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiStatusRefresh);
    } else if(button == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FuseRadioCustomEventWifiStatusMenu);
    }
}

static void
    fuse_radio_app_wifi_mdns_button_callback(GuiButtonType button, InputType type, void* context) {
    FuseRadioApp* app = context;

    if(button == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiMdnsRefresh);
    }
}

static void fuse_radio_app_wifi_discover_button_callback(
    GuiButtonType button,
    InputType type,
    void* context) {
    FuseRadioApp* app = context;

    if(button == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventWifiDiscoverRefresh);
    }
}

static void fuse_radio_app_tick_event_callback(void* context) {
    FuseRadioApp* app = context;
    fuse_radio_app_handle_tick(app);
    scene_manager_handle_tick_event(app->scene_manager);
}

static void fuse_radio_app_serial_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    FuseRadioApp* app = context;
    UNUSED(handle);

    if(event != FuriHalSerialRxEventData) {
        return;
    }

    const uint8_t byte = furi_hal_serial_async_rx(app->serial_handle);
    furi_stream_buffer_send(app->rx_stream, &byte, sizeof(byte), 0U);
}

static void fuse_radio_app_reset_scan_results(FuseRadioApp* app) {
    memset(&app->scan_results, 0, sizeof(app->scan_results));
    app->scan_dirty = true;
}

static void fuse_radio_app_reset_mdns_results(FuseRadioApp* app) {
    app->mdns_info_text[0] = '\0';
    app->mdns_count = 0U;
    app->mdns_truncated_count = 0U;
    app->mdns_dirty = true;
}

static void fuse_radio_app_reset_discover_results(FuseRadioApp* app) {
    app->discover_info_text[0] = '\0';
    app->discover_scanned_count = 0U;
    app->discover_found_count = 0U;
    app->discover_duration_ms = 0U;
    app->discover_dirty = true;
}

static void fuse_radio_app_set_wifi_info_text(FuseRadioApp* app, const char* text) {
    strncpy(app->wifi_info_text, text, sizeof(app->wifi_info_text) - 1U);
    app->wifi_info_text[sizeof(app->wifi_info_text) - 1U] = '\0';
    app->wifi_info_dirty = true;
}

static void fuse_radio_app_append_mdns_text(FuseRadioApp* app, const char* fmt, ...) {
    const size_t used = strlen(app->mdns_info_text);
    if(used >= sizeof(app->mdns_info_text) - 1U) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(app->mdns_info_text + used, sizeof(app->mdns_info_text) - used, fmt, args);
    va_end(args);
    app->mdns_dirty = true;
}

static void fuse_radio_app_append_discover_text(FuseRadioApp* app, const char* fmt, ...) {
    const size_t used = strlen(app->discover_info_text);
    if(used >= sizeof(app->discover_info_text) - 1U) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(app->discover_info_text + used, sizeof(app->discover_info_text) - used, fmt, args);
    va_end(args);
    app->discover_dirty = true;
}

static void fuse_radio_app_reset_wifi_status(FuseRadioApp* app) {
    app->wifi_connected = false;
    app->wifi_status_reason = 0U;
    app->connect_password_auto_used = false;
    strncpy(app->wifi_status_state, "IDLE", sizeof(app->wifi_status_state) - 1U);
    app->wifi_status_state[sizeof(app->wifi_status_state) - 1U] = '\0';
    strncpy(app->wifi_status_ssid, "-", sizeof(app->wifi_status_ssid) - 1U);
    app->wifi_status_ssid[sizeof(app->wifi_status_ssid) - 1U] = '\0';
    fuse_radio_app_set_wifi_info_text(app, "Request a status update to poll the ESP.");
}

static void fuse_radio_app_update_wifi_info_text(FuseRadioApp* app) {
    snprintf(
        app->wifi_info_text,
        sizeof(app->wifi_info_text),
        "State: %s\nConnected: %s\nSSID: %s\nReason: %u",
        app->wifi_status_state,
        app->wifi_connected ? "yes" : "no",
        app->wifi_status_ssid,
        (unsigned)app->wifi_status_reason);
    app->wifi_info_dirty = true;
}

static bool fuse_radio_app_enable_otg(FuseRadioApp* app) {
    furi_assert(app);

    if(!app->power) {
        app->power = furi_record_open(RECORD_POWER);
    }

    PowerInfo power_info = {0};
    power_get_info(app->power, &power_info);
    if(power_info.voltage_vbus >= 4.5f) {
        app->otg_enabled = false;
        fuse_radio_app_set_status(app, "USB already provides 5V to VBUS.");
        return true;
    }

    power_enable_otg(app->power, true);
    power_get_info(app->power, &power_info);
    if(!power_info.is_otg_enabled) {
        fuse_radio_app_set_error(app, "Cannot enable OTG 5V");
        return false;
    }

    app->otg_enabled = true;
    return true;
}

static void fuse_radio_app_disable_otg(FuseRadioApp* app) {
    furi_assert(app);

    if(app->power && app->otg_enabled) {
        power_enable_otg(app->power, false);
        app->otg_enabled = false;
    }
}

void fuse_radio_app_set_status(FuseRadioApp* app, const char* detail) {
    furi_assert(app);
    furi_assert(detail);

    strncpy(app->status_detail, detail, sizeof(app->status_detail) - 1U);
    app->status_detail[sizeof(app->status_detail) - 1U] = '\0';
    app->status_dirty = true;
}

void fuse_radio_app_set_error(FuseRadioApp* app, const char* error) {
    furi_assert(app);
    furi_assert(error);

    app->module_state = FuseRadioModuleStateError;
    strncpy(app->last_error, error, sizeof(app->last_error) - 1U);
    app->last_error[sizeof(app->last_error) - 1U] = '\0';
    app->status_dirty = true;
}

static bool fuse_radio_app_send_line(FuseRadioApp* app, const char* line) {
    if(!app->serial_handle) {
        return false;
    }

    const size_t length = strlen(line);
    furi_hal_serial_tx(app->serial_handle, (const uint8_t*)line, length);
    furi_hal_serial_tx_wait_complete(app->serial_handle);
    return true;
}

bool fuse_radio_app_send_ping(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "PING\n");
}

static bool fuse_radio_app_send_wifi_scan(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "WIFI SCAN\n");
}

static bool fuse_radio_app_format_quoted_arg(char* out, size_t out_size, const char* value) {
    size_t out_index = 0U;

    if(out_size < 3U) {
        return false;
    }

    out[out_index++] = '"';
    for(size_t index = 0; value[index] != '\0'; index++) {
        const char ch = value[index];
        if(ch == '"' || ch == '\\') {
            if(out_index + 2U >= out_size) {
                return false;
            }
            out[out_index++] = '\\';
        } else if(out_index + 1U >= out_size) {
            return false;
        }

        out[out_index++] = ch;
    }

    if(out_index + 2U > out_size) {
        return false;
    }

    out[out_index++] = '"';
    out[out_index] = '\0';
    return true;
}

static bool fuse_radio_app_send_wifi_connect_command(
    FuseRadioApp* app,
    const char* ssid,
    const char* password) {
    char quoted_ssid[96];
    char quoted_password[160];
    char command[320];

    if(!fuse_radio_app_format_quoted_arg(quoted_ssid, sizeof(quoted_ssid), ssid) ||
       !fuse_radio_app_format_quoted_arg(quoted_password, sizeof(quoted_password), password)) {
        return false;
    }

    snprintf(
        command, sizeof(command), "WIFI CONNECT ssid=%s psw=%s\n", quoted_ssid, quoted_password);
    return fuse_radio_app_send_line(app, command);
}

static bool fuse_radio_app_send_wifi_disconnect_command(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "WIFI DISCONNECT\n");
}

static bool fuse_radio_app_send_wifi_discover_command(FuseRadioApp* app) {
    return fuse_radio_app_send_line(app, "WIFI DISCOVER\n");
}

static bool fuse_radio_app_send_wifi_mdns_command(FuseRadioApp* app, const char* host) {
    char command[128];
    snprintf(command, sizeof(command), "WIFI READ_MDNS host=%s\n", host);
    return fuse_radio_app_send_line(app, command);
}

bool fuse_radio_app_request_wifi_status(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_wifi_info_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestStatus;
    fuse_radio_app_set_wifi_info_text(app, "Requesting WiFi status...");
    if(!fuse_radio_app_send_line(app, "WIFI STATUS\n")) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_set_wifi_info_text(app, "UART write failed while requesting status.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_connect(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_wifi_info_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestConnect;
    app->wifi_state = FuseRadioWifiStateConnectRequested;
    snprintf(
        app->wifi_info_text,
        sizeof(app->wifi_info_text),
        "Connecting to %s...\nUse Refresh to poll status.",
        app->connect_ssid);
    app->wifi_info_dirty = true;

    if(!fuse_radio_app_send_wifi_connect_command(app, app->connect_ssid, app->connect_password)) {
        app->current_request = FuseRadioRequestNone;
        app->wifi_state = FuseRadioWifiStateError;
        fuse_radio_app_set_wifi_info_text(app, "UART write failed while starting connect.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_disconnect(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_wifi_info_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestDisconnect;
    app->wifi_state = FuseRadioWifiStateDisconnecting;
    fuse_radio_app_set_wifi_info_text(app, "Disconnecting from the active AP...");

    if(!fuse_radio_app_send_wifi_disconnect_command(app)) {
        app->current_request = FuseRadioRequestNone;
        app->wifi_state = FuseRadioWifiStateError;
        fuse_radio_app_set_wifi_info_text(app, "UART write failed while disconnecting.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_discover(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_reset_discover_results(app);
        fuse_radio_app_append_discover_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestDiscover;
    fuse_radio_app_reset_discover_results(app);
    fuse_radio_app_append_discover_text(app, "Probing local subnet...\n");

    if(!fuse_radio_app_send_wifi_discover_command(app)) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_reset_discover_results(app);
        fuse_radio_app_append_discover_text(app, "UART write failed while starting discovery.");
        return false;
    }

    return true;
}

bool fuse_radio_app_start_wifi_mdns_query(FuseRadioApp* app) {
    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_reset_mdns_results(app);
        fuse_radio_app_append_mdns_text(app, "Board is not ready.");
        return false;
    }

    app->current_request = FuseRadioRequestMdns;
    fuse_radio_app_reset_mdns_results(app);
    fuse_radio_app_append_mdns_text(app, "Querying %s...\n", app->mdns_host);

    if(!fuse_radio_app_send_wifi_mdns_command(app, app->mdns_host)) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_reset_mdns_results(app);
        fuse_radio_app_append_mdns_text(app, "UART write failed while querying mDNS.");
        return false;
    }

    return true;
}

static void fuse_radio_app_mark_detected(FuseRadioApp* app) {
    app->module_state = FuseRadioModuleStateDetected;
    app->module_detect_event_sent = false;
    app->last_error[0] = '\0';
    fuse_radio_app_set_status(app, "Board detected. Opening control menu.");
}

static void fuse_radio_app_parse_mdns_line(FuseRadioApp* app, const char* line) {
    const char* host = strstr(line, "host=");
    const char* addr = strstr(line, " addr=");
    const char* family = strstr(line, " family=");

    if(!host || !addr || !family) {
        fuse_radio_app_append_mdns_text(app, "%s\n", line);
        return;
    }

    host += 5;
    addr += 6;
    family += 8;

    char host_text[64] = {0};
    char addr_text[64] = {0};
    char family_text[16] = {0};
    size_t host_len = (size_t)(strstr(host, " addr=") - host);
    size_t addr_len = (size_t)(strstr(addr, " family=") - addr);
    size_t family_len = strcspn(family, " ");

    if(host_len >= sizeof(host_text)) host_len = sizeof(host_text) - 1U;
    if(addr_len >= sizeof(addr_text)) addr_len = sizeof(addr_text) - 1U;
    if(family_len >= sizeof(family_text)) family_len = sizeof(family_text) - 1U;

    memcpy(host_text, host, host_len);
    memcpy(addr_text, addr, addr_len);
    memcpy(family_text, family, family_len);

    fuse_radio_app_append_mdns_text(app, "%s\n%s (%s)\n\n", host_text, addr_text, family_text);
}

static void fuse_radio_app_parse_discover_network_line(FuseRadioApp* app, const char* line) {
    const char* subnet = strstr(line, "subnet=");
    const char* self = strstr(line, " self=");

    if(!subnet || !self) {
        fuse_radio_app_append_discover_text(app, "%s\n", line);
        return;
    }

    subnet += 7;
    self += 6;

    char subnet_text[32] = {0};
    char self_text[32] = {0};
    size_t subnet_len = (size_t)(strstr(subnet, " self=") - subnet);
    size_t self_len = strcspn(self, " ");

    if(subnet_len >= sizeof(subnet_text)) subnet_len = sizeof(subnet_text) - 1U;
    if(self_len >= sizeof(self_text)) self_len = sizeof(self_text) - 1U;

    memcpy(subnet_text, subnet, subnet_len);
    memcpy(self_text, self, self_len);
    fuse_radio_app_append_discover_text(app, "Subnet: %s\nSelf: %s\n\n", subnet_text, self_text);
}

static void fuse_radio_app_parse_discover_found_line(FuseRadioApp* app, const char* line) {
    const char* ip = strstr(line, "ip=");
    const char* host = strstr(line, " host=");
    const char* rtt = strstr(line, " rtt_ms=");

    if(!ip || !host || !rtt) {
        fuse_radio_app_append_discover_text(app, "%s\n", line);
        return;
    }

    ip += 3;
    host += 6;
    rtt += 8;

    char ip_text[32] = {0};
    char host_text[64] = {0};
    char rtt_text[16] = {0};
    size_t ip_len = (size_t)(strstr(ip, " host=") - ip);
    size_t host_len = (size_t)(strstr(host, " rtt_ms=") - host);
    size_t rtt_len = strcspn(rtt, " ");

    if(ip_len >= sizeof(ip_text)) ip_len = sizeof(ip_text) - 1U;
    if(host_len >= sizeof(host_text)) host_len = sizeof(host_text) - 1U;
    if(rtt_len >= sizeof(rtt_text)) rtt_len = sizeof(rtt_text) - 1U;

    memcpy(ip_text, ip, ip_len);
    memcpy(host_text, host, host_len);
    memcpy(rtt_text, rtt, rtt_len);

    fuse_radio_app_append_discover_text(
        app,
        "%s\n%s  %sms\n\n",
        ip_text,
        strcmp(host_text, "-") == 0 ? "host:-" : host_text,
        rtt_text);
}

static bool fuse_radio_app_parse_ap_line(FuseRadioApp* app, const char* line) {
    if(app->scan_results.count >= FUSE_RADIO_MAX_AP_COUNT) {
        return false;
    }

    const char* ssid_begin = line + 3;
    const char* rssi_tag = strstr(ssid_begin, " RSSI ");
    const char* auth_tag = rssi_tag ? strstr(rssi_tag + 6, " AUTH ") : NULL;
    const char* ch_tag = auth_tag ? strstr(auth_tag + 6, " CH ") : NULL;

    if(!rssi_tag || !auth_tag || !ch_tag) {
        return false;
    }

    FuseRadioAccessPoint* ap = &app->scan_results.aps[app->scan_results.count];
    const size_t ssid_size = (size_t)(rssi_tag - ssid_begin);
    const size_t auth_size = (size_t)(ch_tag - (auth_tag + 6));
    int rssi = 0;
    unsigned long channel = 0;

    if(ssid_size > FUSE_RADIO_MAX_SSID_LENGTH || auth_size > FUSE_RADIO_MAX_AUTH_LENGTH) {
        return false;
    }

    memcpy(ap->ssid, ssid_begin, ssid_size);
    ap->ssid[ssid_size] = '\0';
    memcpy(ap->auth, auth_tag + 6, auth_size);
    ap->auth[auth_size] = '\0';

    rssi = (int)strtol(rssi_tag + 6, NULL, 10);
    channel = strtoul(ch_tag + 4, NULL, 10);

    ap->rssi = (int16_t)rssi;
    ap->channel = (uint8_t)channel;

    app->scan_results.count++;
    app->scan_dirty = true;
    return true;
}

static void fuse_radio_app_parse_wifi_status(FuseRadioApp* app, const char* line) {
    const FuseRadioWifiState previous_wifi_state = app->wifi_state;
    const char* state = strstr(line, "state=");
    const char* connected = strstr(line, "connected=");
    const char* ssid = strstr(line, "ssid=");
    const char* reason = strstr(line, "reason=");

    if(state) {
        state += 6;
        size_t length = strcspn(state, " ");
        if(length >= sizeof(app->wifi_status_state)) {
            length = sizeof(app->wifi_status_state) - 1U;
        }
        memcpy(app->wifi_status_state, state, length);
        app->wifi_status_state[length] = '\0';
    }

    if(connected) {
        app->wifi_connected = (strncmp(connected + 10, "yes", 3) == 0);
    }

    if(ssid) {
        ssid += 5;
        size_t length = strcspn(ssid, " ");
        if(length >= sizeof(app->wifi_status_ssid)) {
            length = sizeof(app->wifi_status_ssid) - 1U;
        }
        memcpy(app->wifi_status_ssid, ssid, length);
        app->wifi_status_ssid[length] = '\0';
    }

    if(reason) {
        app->wifi_status_reason = (uint16_t)strtoul(reason + 7, NULL, 10);
    }

    if(strcmp(app->wifi_status_state, "CONNECTED") == 0) {
        app->wifi_state = FuseRadioWifiStateConnected;
    } else if(strcmp(app->wifi_status_state, "CONNECTING") == 0) {
        app->wifi_state = FuseRadioWifiStateConnecting;
    } else if(strcmp(app->wifi_status_state, "SCANNING") == 0) {
        app->wifi_state = FuseRadioWifiStateScanning;
    } else {
        app->wifi_state = FuseRadioWifiStateIdle;
    }

    if(
        app->wifi_connected && app->connect_ssid[0] && app->connect_password[0] &&
        (previous_wifi_state == FuseRadioWifiStateConnectRequested ||
         previous_wifi_state == FuseRadioWifiStateConnecting ||
         app->current_request == FuseRadioRequestConnect)) {
        fuse_radio_app_store_saved_credential(app, app->connect_ssid, app->connect_password);
        app->skip_auto_password_ssid[0] = '\0';
        app->connect_password_saved = true;
        app->connect_password_auto_used = false;
    } else if(
        app->connect_password_auto_used && !app->wifi_connected &&
        previous_wifi_state == FuseRadioWifiStateConnecting &&
        strcmp(app->wifi_status_state, "CONNECTING") != 0) {
        fuse_radio_app_strlcpy(
            app->skip_auto_password_ssid,
            app->connect_ssid,
            sizeof(app->skip_auto_password_ssid));
        app->connect_password_auto_used = false;
        app->connect_password_saved = false;
    }

    fuse_radio_app_update_wifi_info_text(app);
}

static void fuse_radio_app_handle_error_line(FuseRadioApp* app, const char* line) {
    if(app->current_request == FuseRadioRequestScan ||
       app->wifi_state == FuseRadioWifiStateScanning ||
       app->wifi_state == FuseRadioWifiStateScanRequested) {
        app->wifi_state = FuseRadioWifiStateError;
        app->scan_results.has_error = true;
        strncpy(app->scan_results.error, line, sizeof(app->scan_results.error) - 1U);
        app->scan_results.error[sizeof(app->scan_results.error) - 1U] = '\0';
        app->scan_results.is_complete = true;
        app->scan_dirty = true;
        app->current_request = FuseRadioRequestNone;
        return;
    }

    if(app->current_request == FuseRadioRequestMdns) {
        fuse_radio_app_reset_mdns_results(app);
        fuse_radio_app_append_mdns_text(app, "%s", line);
        app->current_request = FuseRadioRequestNone;
        return;
    }

    if(app->current_request == FuseRadioRequestDiscover) {
        fuse_radio_app_reset_discover_results(app);
        fuse_radio_app_append_discover_text(app, "%s", line);
        app->current_request = FuseRadioRequestNone;
        return;
    }

    if(app->current_request == FuseRadioRequestStatus ||
       app->current_request == FuseRadioRequestConnect ||
       app->current_request == FuseRadioRequestDisconnect) {
        if(app->connect_password_auto_used &&
           (app->current_request == FuseRadioRequestStatus ||
            app->current_request == FuseRadioRequestConnect)) {
            fuse_radio_app_strlcpy(
                app->skip_auto_password_ssid,
                app->connect_ssid,
                sizeof(app->skip_auto_password_ssid));
            app->connect_password_auto_used = false;
            app->connect_password_saved = false;
        }
        app->wifi_state = FuseRadioWifiStateError;
        fuse_radio_app_set_wifi_info_text(app, line);
        app->current_request = FuseRadioRequestNone;
        return;
    }

    if(app->module_state != FuseRadioModuleStateDetected) {
        fuse_radio_app_set_error(app, line);
    }
}

static void fuse_radio_app_handle_line(FuseRadioApp* app, const char* line) {
    if(strcmp(line, "READY") == 0 || strcmp(line, "PONG") == 0) {
        fuse_radio_app_mark_detected(app);
    } else if(strncmp(line, "SCAN_COUNT ", 11) == 0) {
        app->current_request = FuseRadioRequestScan;
        app->scan_results.total_count = (uint16_t)strtoul(line + 11, NULL, 10);
        app->wifi_state = FuseRadioWifiStateScanning;
        app->scan_dirty = true;
    } else if(strncmp(line, "AP ", 3) == 0) {
        fuse_radio_app_parse_ap_line(app, line);
    } else if(strncmp(line, "SCAN_TRUNCATED ", 15) == 0) {
        app->scan_results.truncated_count = (uint16_t)strtoul(line + 15, NULL, 10);
        app->scan_dirty = true;
    } else if(strcmp(line, "SCAN_DONE") == 0) {
        app->current_request = FuseRadioRequestNone;
        app->wifi_state = FuseRadioWifiStateScanComplete;
        app->scan_results.is_complete = true;
        app->scan_dirty = true;
    } else if(strcmp(line, "WIFI CONNECTING") == 0) {
        app->current_request = FuseRadioRequestNone;
        app->wifi_state = FuseRadioWifiStateConnecting;
        snprintf(
            app->wifi_info_text,
            sizeof(app->wifi_info_text),
            "Connecting to %s...\nUse Refresh to poll status.",
            app->connect_ssid);
        app->wifi_info_dirty = true;
    } else if(strcmp(line, "WIFI DISCONNECTING") == 0) {
        app->current_request = FuseRadioRequestNone;
        app->wifi_state = FuseRadioWifiStateDisconnecting;
        fuse_radio_app_set_wifi_info_text(app, "Disconnecting...\nUse Refresh to confirm state.");
    } else if(strncmp(line, "WIFI STATUS ", 12) == 0) {
        app->current_request = FuseRadioRequestNone;
        fuse_radio_app_parse_wifi_status(app, line);
    } else if(strncmp(line, "MDNS_COUNT ", 11) == 0) {
        app->mdns_count = (uint8_t)strtoul(line + 11, NULL, 10);
        fuse_radio_app_reset_mdns_results(app);
        fuse_radio_app_append_mdns_text(app, "Results: %u\n\n", (unsigned)app->mdns_count);
    } else if(strncmp(line, "DISCOVER_NETWORK ", 17) == 0) {
        fuse_radio_app_reset_discover_results(app);
        fuse_radio_app_parse_discover_network_line(app, line);
    } else if(strncmp(line, "DISCOVER_FOUND ", 15) == 0) {
        fuse_radio_app_parse_discover_found_line(app, line);
    } else if(strncmp(line, "DISCOVER_DONE ", 14) == 0) {
        const char* scanned = strstr(line, "scanned=");
        const char* found = strstr(line, " found=");
        const char* duration = strstr(line, " duration_ms=");

        if(scanned) {
            app->discover_scanned_count = (uint16_t)strtoul(scanned + 8, NULL, 10);
        }
        if(found) {
            app->discover_found_count = (uint16_t)strtoul(found + 7, NULL, 10);
        }
        if(duration) {
            app->discover_duration_ms = (uint32_t)strtoul(duration + 13, NULL, 10);
        }

        fuse_radio_app_append_discover_text(
            app,
            "Summary: %u scanned, %u found\n%lu ms",
            (unsigned)app->discover_scanned_count,
            (unsigned)app->discover_found_count,
            (unsigned long)app->discover_duration_ms);
        app->current_request = FuseRadioRequestNone;
    } else if(strncmp(line, "MDNS host=", 10) == 0) {
        fuse_radio_app_parse_mdns_line(app, line);
    } else if(strncmp(line, "MDNS_TRUNCATED ", 15) == 0) {
        app->mdns_truncated_count = (uint8_t)strtoul(line + 15, NULL, 10);
        fuse_radio_app_append_mdns_text(
            app, "+%u more results omitted\n", (unsigned)app->mdns_truncated_count);
    } else if(strcmp(line, "MDNS_DONE") == 0) {
        if(app->mdns_count == 0U && app->mdns_info_text[0] == '\0') {
            fuse_radio_app_append_mdns_text(app, "No mDNS results.");
        }
        app->current_request = FuseRadioRequestNone;
    } else if(strncmp(line, "ERR ", 4) == 0) {
        fuse_radio_app_handle_error_line(app, line);
    }
}

void fuse_radio_app_process_rx(FuseRadioApp* app) {
    uint8_t byte = 0;

    while(furi_stream_buffer_receive(app->rx_stream, &byte, sizeof(byte), 0U) == sizeof(byte)) {
        if(byte == '\r') {
            continue;
        }

        if(byte == '\n') {
            if(!app->line_overflow && app->line_length > 0U) {
                app->line_buffer[app->line_length] = '\0';
                fuse_radio_app_handle_line(app, app->line_buffer);
            }

            app->line_length = 0U;
            app->line_overflow = false;
            continue;
        }

        if(app->line_length >= FUSE_RADIO_MAX_LINE_LENGTH) {
            app->line_overflow = true;
            continue;
        }

        app->line_buffer[app->line_length++] = (char)byte;
    }
}

bool fuse_radio_app_start_session(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_app_reset_scan_results(app);
    fuse_radio_app_reset_discover_results(app);
    fuse_radio_app_reset_mdns_results(app);
    fuse_radio_app_reset_wifi_status(app);
    app->current_request = FuseRadioRequestNone;
    app->text_input_mode = FuseRadioTextInputNone;
    app->line_length = 0U;
    app->line_overflow = false;
    app->module_detect_event_sent = false;
    app->last_error[0] = '\0';
    app->module_state = FuseRadioModuleStatePowering;
    fuse_radio_app_set_status(app, "Powering ESP module from OTG.");

    if(!app->expansion) {
        app->expansion = furi_record_open(RECORD_EXPANSION);
    }

    if(app->expansion && !app->expansion_disabled) {
        expansion_disable(app->expansion);
        app->expansion_disabled = true;
    }

    if(!app->otg_enabled) {
        if(!fuse_radio_app_enable_otg(app)) {
            return false;
        }
    }

    if(app->otg_enabled) {
        fuse_radio_app_set_status(app, "OTG 5V enabled. Waiting for module.");
    } else {
        fuse_radio_app_set_status(app, "USB 5V detected. Waiting for module.");
    }

    if(!app->insomnia_active) {
        furi_hal_power_insomnia_enter();
        app->insomnia_active = true;
    }

    app->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app->serial_handle) {
        fuse_radio_app_set_error(app, "Cannot acquire USART");
        return false;
    }

    furi_hal_serial_init(app->serial_handle, FUSE_RADIO_UART_BAUD_RATE);
    furi_hal_serial_configure_framing(
        app->serial_handle,
        FuriHalSerialDataBits8,
        FuriHalSerialParityNone,
        FuriHalSerialStopBits1);
    furi_hal_serial_async_rx_start(
        app->serial_handle, fuse_radio_app_serial_rx_callback, app, false);
    app->rx_started = true;

    app->detect_started_at = furi_get_tick();
    app->last_ping_at = 0U;
    app->module_state = FuseRadioModuleStateWaitingReady;
    fuse_radio_app_set_status(app, "Waiting for READY or PONG.");

    return true;
}

void fuse_radio_app_stop_session(FuseRadioApp* app) {
    furi_assert(app);

    if(app->rx_started && app->serial_handle) {
        furi_hal_serial_async_rx_stop(app->serial_handle);
        app->rx_started = false;
    }

    if(app->serial_handle) {
        furi_hal_serial_deinit(app->serial_handle);
        furi_hal_serial_control_release(app->serial_handle);
        app->serial_handle = NULL;
    }

    fuse_radio_app_disable_otg(app);

    if(app->insomnia_active) {
        furi_hal_power_insomnia_exit();
        app->insomnia_active = false;
    }

    if(app->expansion && app->expansion_disabled) {
        expansion_enable(app->expansion);
        app->expansion_disabled = false;
    }

    if(app->expansion) {
        furi_record_close(RECORD_EXPANSION);
        app->expansion = NULL;
    }

    if(app->power) {
        furi_record_close(RECORD_POWER);
        app->power = NULL;
    }

    app->module_state = FuseRadioModuleStateInactive;
}

void fuse_radio_app_retry_session(FuseRadioApp* app) {
    furi_assert(app);
    fuse_radio_app_stop_session(app);
    fuse_radio_app_start_session(app);
}

bool fuse_radio_app_start_wifi_scan(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_app_reset_scan_results(app);
    app->current_request = FuseRadioRequestScan;
    app->wifi_state = FuseRadioWifiStateScanRequested;
    app->scan_dirty = true;

    if(app->module_state != FuseRadioModuleStateDetected) {
        app->wifi_state = FuseRadioWifiStateError;
        app->scan_results.has_error = true;
        strncpy(
            app->scan_results.error, "Board is not ready", sizeof(app->scan_results.error) - 1U);
        app->scan_results.error[sizeof(app->scan_results.error) - 1U] = '\0';
        return false;
    }

    if(!fuse_radio_app_send_wifi_scan(app)) {
        app->wifi_state = FuseRadioWifiStateError;
        app->scan_results.has_error = true;
        strncpy(
            app->scan_results.error, "UART write failed", sizeof(app->scan_results.error) - 1U);
        app->scan_results.error[sizeof(app->scan_results.error) - 1U] = '\0';
        app->scan_dirty = true;
        return false;
    }

    app->wifi_state = FuseRadioWifiStateScanning;
    app->scan_dirty = true;
    return true;
}

void fuse_radio_app_refresh_status_widget(FuseRadioApp* app) {
    furi_assert(app);

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, "Fuse Radio");

    const char* headline = "Starting";
    const char* detail = app->status_detail;

    if(app->module_state == FuseRadioModuleStatePowering) {
        headline = "Powering ESP";
    } else if(app->module_state == FuseRadioModuleStateWaitingReady) {
        headline = "Checking Link";
    } else if(app->module_state == FuseRadioModuleStateDetected) {
        headline = "Board Ready";
    } else if(app->module_state == FuseRadioModuleStateError) {
        headline = "No Module";
        detail = app->last_error;
    }

    widget_add_string_multiline_element(
        app->widget, 64, 28, AlignCenter, AlignCenter, FontSecondary, headline);
    widget_add_string_multiline_element(
        app->widget, 64, 44, AlignCenter, AlignCenter, FontSecondary, detail);

    if(app->module_state == FuseRadioModuleStateError) {
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, "Retry", fuse_radio_app_status_button_callback, app);
    }

    app->status_dirty = false;
}

void fuse_radio_app_refresh_wifi_info_widget(FuseRadioApp* app) {
    furi_assert(app);

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, "WiFi Status");
    widget_add_text_scroll_element(app->widget, 0, 15, 128, 38, app->wifi_info_text);
    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "Menu", fuse_radio_app_wifi_status_button_callback, app);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "Refresh",
        fuse_radio_app_wifi_status_button_callback,
        app);

    app->wifi_info_dirty = false;
}

void fuse_radio_app_refresh_discover_widget(FuseRadioApp* app) {
    furi_assert(app);

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, "Discover");
    widget_add_text_scroll_element(app->widget, 0, 15, 128, 38, app->discover_info_text);
    widget_add_button_element(
        app->widget,
        GuiButtonTypeCenter,
        "Probe",
        fuse_radio_app_wifi_discover_button_callback,
        app);

    app->discover_dirty = false;
}

void fuse_radio_app_refresh_mdns_widget(FuseRadioApp* app) {
    furi_assert(app);

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, "mDNS");
    widget_add_text_scroll_element(app->widget, 0, 15, 128, 38, app->mdns_info_text);
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Query", fuse_radio_app_wifi_mdns_button_callback, app);

    app->mdns_dirty = false;
}

void fuse_radio_app_refresh_scan_view(FuseRadioApp* app) {
    furi_assert(app);
    fuse_radio_scan_view_set_data(app->scan_view, &app->scan_results, app->wifi_state);
    app->scan_dirty = false;
}

void fuse_radio_app_handle_tick(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_app_process_rx(app);

    if(app->module_state == FuseRadioModuleStateWaitingReady) {
        const uint32_t now = furi_get_tick();

        if((now - app->detect_started_at) >= FUSE_RADIO_DETECT_TIMEOUT_MS) {
            fuse_radio_app_set_error(app, "No UART response\nfrom the ESP board");
        } else if(
            (app->last_ping_at == 0U) ||
            ((now - app->last_ping_at) >= FUSE_RADIO_PING_INTERVAL_MS)) {
            if(fuse_radio_app_send_ping(app)) {
                app->last_ping_at = now;
                fuse_radio_app_set_status(app, "Sent PING. Waiting for PONG.");
            }
        }
    }

    if((app->module_state == FuseRadioModuleStateDetected) && !app->module_detect_event_sent) {
        app->module_detect_event_sent = true;
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FuseRadioCustomEventModuleDetected);
    }

    const uint32_t scene = scene_manager_get_current_scene(app->scene_manager);
    if(app->status_dirty && scene == FuseRadioSceneStatus) {
        fuse_radio_app_refresh_status_widget(app);
    }
    if(app->scan_dirty &&
       (scene == FuseRadioSceneWifiScan || scene == FuseRadioSceneWifiConnectSsid)) {
        fuse_radio_app_refresh_scan_view(app);
    }
    if(app->wifi_info_dirty && scene == FuseRadioSceneWifiStatus) {
        fuse_radio_app_refresh_wifi_info_widget(app);
    }
    if(app->discover_dirty && scene == FuseRadioSceneWifiDiscoverResult) {
        fuse_radio_app_refresh_discover_widget(app);
    }
    if(app->mdns_dirty && scene == FuseRadioSceneWifiMdnsResult) {
        fuse_radio_app_refresh_mdns_widget(app);
    }
}

FuseRadioApp* fuse_radio_app_alloc(void) {
    FuseRadioApp* app = malloc(sizeof(FuseRadioApp));
    memset(app, 0, sizeof(FuseRadioApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&fuse_radio_scene_handlers, app);
    app->widget = widget_alloc();
    app->submenu = submenu_alloc();
    app->scan_view = fuse_radio_scan_view_alloc();
    app->text_input = text_input_alloc();
    app->rx_stream = furi_stream_buffer_alloc(FUSE_RADIO_RX_STREAM_SIZE, 1U);
    app->power = furi_record_open(RECORD_POWER);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->credentials_format = flipper_format_file_alloc(app->storage);

    fuse_radio_app_reset_wifi_status(app);
    fuse_radio_app_set_status(app, "Preparing module session.");
    fuse_radio_app_load_credentials(app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, fuse_radio_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, fuse_radio_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, fuse_radio_app_tick_event_callback, 100U);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewWidget, widget_get_view(app->widget));
    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewScan, fuse_radio_scan_view_get_view(app->scan_view));
    view_dispatcher_add_view(
        app->view_dispatcher, FuseRadioViewTextInput, text_input_get_view(app->text_input));

    fuse_radio_scan_view_set_callback(app->scan_view, fuse_radio_app_scan_view_callback, app);

    fuse_radio_app_start_session(app);
    scene_manager_next_scene(app->scene_manager, FuseRadioSceneStatus);

    return app;
}

void fuse_radio_app_run(FuseRadioApp* app) {
    furi_assert(app);
    view_dispatcher_run(app->view_dispatcher);
}

void fuse_radio_app_free(FuseRadioApp* app) {
    furi_assert(app);

    fuse_radio_app_stop_session(app);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewScan);
    fuse_radio_scan_view_free(app->scan_view);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewTextInput);
    text_input_free(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewSubmenu);
    submenu_free(app->submenu);

    view_dispatcher_remove_view(app->view_dispatcher, FuseRadioViewWidget);
    widget_free(app->widget);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_stream_buffer_free(app->rx_stream);

    if(app->power) {
        furi_record_close(RECORD_POWER);
        app->power = NULL;
    }

    if(app->credentials_format) {
        flipper_format_free(app->credentials_format);
        app->credentials_format = NULL;
    }

    if(app->storage) {
        furi_record_close(RECORD_STORAGE);
        app->storage = NULL;
    }

    furi_record_close(RECORD_GUI);
    free(app);
}
