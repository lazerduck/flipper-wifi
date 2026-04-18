#include "modules/wifi/connected/wifi_http.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"

#define WIFI_HTTP_BODY_MAX_LENGTH 768
#define WIFI_HTTP_RESULT_LINE_MAX_LENGTH 192

typedef enum {
    WIFI_HTTP_PRESET_IP = 0,
    WIFI_HTTP_PRESET_TIME,
    WIFI_HTTP_PRESET_LOCATION,
} wifi_http_preset_t;

typedef struct {
    const char *name;
    const char *url;
    wifi_http_preset_t preset;
} wifi_http_preset_config_t;

typedef struct {
    char body[WIFI_HTTP_BODY_MAX_LENGTH];
    size_t length;
} wifi_http_response_buffer_t;

static const wifi_http_preset_config_t s_wifi_http_presets[] = {
    {.name = "ip", .url = "http://api.ipify.org/?format=text", .preset = WIFI_HTTP_PRESET_IP},
    {.name = "time", .url = "http://worldclockapi.com/api/json/utc/now", .preset = WIFI_HTTP_PRESET_TIME},
    {.name = "location", .url = "http://ipwho.is/?output=json", .preset = WIFI_HTTP_PRESET_LOCATION},
};

static esp_err_t wifi_http_event_handler(esp_http_client_event_t *event)
{
    wifi_http_response_buffer_t *buffer = (wifi_http_response_buffer_t *)event->user_data;

    if (buffer == NULL) {
        return ESP_OK;
    }

    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != NULL && event->data_len > 0) {
        const size_t remaining = sizeof(buffer->body) - buffer->length - 1U;

        if ((size_t)event->data_len > remaining) {
            return ESP_ERR_NO_MEM;
        }

        memcpy(buffer->body + buffer->length, event->data, (size_t)event->data_len);
        buffer->length += (size_t)event->data_len;
        buffer->body[buffer->length] = '\0';
    }

    return ESP_OK;
}

static const wifi_http_preset_config_t *wifi_http_find_preset(const char *preset_name)
{
    size_t index;

    if (preset_name == NULL) {
        return NULL;
    }

    for (index = 0; index < (sizeof(s_wifi_http_presets) / sizeof(s_wifi_http_presets[0])); ++index) {
        if (strcmp(s_wifi_http_presets[index].name, preset_name) == 0) {
            return &s_wifi_http_presets[index];
        }
    }

    return NULL;
}

static bool wifi_http_copy_trimmed(const char *source, char *destination, size_t destination_size)
{
    size_t start = 0U;
    size_t end;

    if (source == NULL || destination == NULL || destination_size == 0U) {
        return false;
    }

    end = strlen(source);
    while (source[start] == ' ' || source[start] == '\t' || source[start] == '\r' || source[start] == '\n') {
        ++start;
    }

    while (end > start) {
        char current = source[end - 1U];
        if (current != ' ' && current != '\t' && current != '\r' && current != '\n') {
            break;
        }

        --end;
    }

    if (end <= start) {
        destination[0] = '\0';
        return false;
    }

    if ((end - start) >= destination_size) {
        return false;
    }

    memcpy(destination, source + start, end - start);
    destination[end - start] = '\0';
    return true;
}

static bool wifi_http_extract_json_string(
    const char *body,
    const char *key,
    char *value,
    size_t value_size)
{
    char pattern[48];
    const char *cursor;
    size_t write_index = 0U;

    if (body == NULL || key == NULL || value == NULL || value_size == 0U) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    cursor = strstr(body, pattern);
    if (cursor == NULL) {
        value[0] = '\0';
        return false;
    }

    cursor += strlen(pattern);
    while (*cursor != '\0' && *cursor != '"') {
        char current = *cursor;

        if (current == '\\' && cursor[1] != '\0') {
            ++cursor;
            current = *cursor;
        }

        if (write_index + 1U >= value_size) {
            return false;
        }

        value[write_index++] = current;
        ++cursor;
    }

    value[write_index] = '\0';
    return *cursor == '"';
}

static bool wifi_http_extract_json_scalar(
    const char *body,
    const char *key,
    char *value,
    size_t value_size)
{
    char pattern[48];
    const char *cursor;
    size_t length = 0U;

    if (body == NULL || key == NULL || value == NULL || value_size == 0U) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    cursor = strstr(body, pattern);
    if (cursor == NULL) {
        value[0] = '\0';
        return false;
    }

    cursor += strlen(pattern);
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }

    while (cursor[length] != '\0' && cursor[length] != ',' && cursor[length] != '}') {
        if (length + 1U >= value_size) {
            return false;
        }

        value[length] = cursor[length];
        ++length;
    }

    value[length] = '\0';
    {
        size_t start = 0U;
        size_t end = strlen(value);

        while (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n') {
            ++start;
        }

        while (end > start) {
            char current = value[end - 1U];
            if (current != ' ' && current != '\t' && current != '\r' && current != '\n') {
                break;
            }

            --end;
        }

        if (start > 0U && end > start) {
            memmove(value, value + start, end - start);
        }

        value[end - start] = '\0';
    }

    return value[0] != '\0';
}

static void wifi_http_write_line(wifi_http_result_writer_t write_line, void *context, const char *text)
{
    char line[WIFI_HTTP_RESULT_LINE_MAX_LENGTH];

    if (write_line == NULL || text == NULL) {
        return;
    }

    snprintf(line, sizeof(line), "HTTP_LINE %s\n", text);
    write_line(line, context);
}

static esp_err_t wifi_http_perform_get(const char *url, wifi_http_response_buffer_t *response)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = wifi_http_event_handler,
        .user_data = response,
        .timeout_ms = 8000,
        .buffer_size = 256,
        .user_agent = "flipper-wifi/1.0",
    };
    esp_http_client_handle_t client;
    esp_err_t err;
    int status_code;

    if (url == NULL || response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(response, 0, sizeof(*response));
    client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status_code != 200) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t wifi_http_write_ip_result(
    const wifi_http_response_buffer_t *response,
    wifi_http_result_writer_t write_line,
    void *context)
{
    char ip_text[64];

    if (response == NULL || !wifi_http_copy_trimmed(response->body, ip_text, sizeof(ip_text))) {
        return ESP_FAIL;
    }

    wifi_http_write_line(write_line, context, "Public IP");
    wifi_http_write_line(write_line, context, ip_text);
    return ESP_OK;
}

static esp_err_t wifi_http_write_time_result(
    const wifi_http_response_buffer_t *response,
    wifi_http_result_writer_t write_line,
    void *context)
{
    char datetime[96];
    char timezone[64];
    char offset[16];
    char day[16];
    char line[WIFI_HTTP_RESULT_LINE_MAX_LENGTH];

    if (response == NULL) {
        return ESP_FAIL;
    }

    if (!wifi_http_extract_json_string(response->body, "currentDateTime", datetime, sizeof(datetime))) {
        return ESP_FAIL;
    }

    wifi_http_extract_json_string(response->body, "timeZoneName", timezone, sizeof(timezone));
    wifi_http_extract_json_string(response->body, "utcOffset", offset, sizeof(offset));
    wifi_http_extract_json_string(response->body, "dayOfTheWeek", day, sizeof(day));

    wifi_http_write_line(write_line, context, "Current time");
    wifi_http_write_line(write_line, context, datetime);
    if (timezone[0] != '\0') {
        if (offset[0] != '\0') {
            snprintf(line, sizeof(line), "TZ: %s (%s)", timezone, offset);
        } else {
            snprintf(line, sizeof(line), "TZ: %s", timezone);
        }
        wifi_http_write_line(write_line, context, line);
    }
    if (day[0] != '\0') {
        snprintf(line, sizeof(line), "Day: %s", day);
        wifi_http_write_line(write_line, context, line);
    }

    return ESP_OK;
}

static esp_err_t wifi_http_write_location_result(
    const wifi_http_response_buffer_t *response,
    wifi_http_result_writer_t write_line,
    void *context)
{
    char status[24];
    char country[64];
    char region[64];
    char city[64];
    char latitude[24];
    char longitude[24];
    char timezone[64];
    char query[64];
    char isp[96];
    char line[WIFI_HTTP_RESULT_LINE_MAX_LENGTH];

    if (response == NULL) {
        return ESP_FAIL;
    }

    if (!wifi_http_extract_json_scalar(response->body, "success", status, sizeof(status)) ||
        strcmp(status, "true") != 0) {
        return ESP_FAIL;
    }

    wifi_http_extract_json_string(response->body, "country", country, sizeof(country));
    wifi_http_extract_json_string(response->body, "region", region, sizeof(region));
    wifi_http_extract_json_string(response->body, "city", city, sizeof(city));
    wifi_http_extract_json_scalar(response->body, "latitude", latitude, sizeof(latitude));
    wifi_http_extract_json_scalar(response->body, "longitude", longitude, sizeof(longitude));
    wifi_http_extract_json_string(response->body, "id", timezone, sizeof(timezone));
    wifi_http_extract_json_string(response->body, "ip", query, sizeof(query));
    wifi_http_extract_json_string(response->body, "isp", isp, sizeof(isp));

    wifi_http_write_line(write_line, context, "Approx location");
    if (city[0] != '\0' || region[0] != '\0' || country[0] != '\0') {
        snprintf(
            line,
            sizeof(line),
            "%.40s, %.40s, %.40s",
            city[0] == '\0' ? "-" : city,
            region[0] == '\0' ? "-" : region,
            country[0] == '\0' ? "-" : country);
        wifi_http_write_line(write_line, context, line);
    }
    if (timezone[0] != '\0') {
        snprintf(line, sizeof(line), "TZ: %s", timezone);
        wifi_http_write_line(write_line, context, line);
    }
    if (latitude[0] != '\0' && longitude[0] != '\0') {
        snprintf(line, sizeof(line), "Coords: %s, %s", latitude, longitude);
        wifi_http_write_line(write_line, context, line);
    }
    if (query[0] != '\0') {
        snprintf(line, sizeof(line), "IP: %s", query);
        wifi_http_write_line(write_line, context, line);
    }
    if (isp[0] != '\0') {
        snprintf(line, sizeof(line), "ISP: %s", isp);
        wifi_http_write_line(write_line, context, line);
    }

    return ESP_OK;
}

esp_err_t wifi_http_fetch_preset(
    const char *preset_name,
    wifi_http_result_writer_t write_line,
    void *context)
{
    const wifi_http_preset_config_t *preset = wifi_http_find_preset(preset_name);
    wifi_http_response_buffer_t response;
    esp_err_t err;

    if (preset == NULL || write_line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = wifi_http_perform_get(preset->url, &response);
    if (err != ESP_OK) {
        return err;
    }

    switch (preset->preset) {
    case WIFI_HTTP_PRESET_IP:
        err = wifi_http_write_ip_result(&response, write_line, context);
        break;
    case WIFI_HTTP_PRESET_TIME:
        err = wifi_http_write_time_result(&response, write_line, context);
        break;
    case WIFI_HTTP_PRESET_LOCATION:
        err = wifi_http_write_location_result(&response, write_line, context);
        break;
    default:
        err = ESP_ERR_INVALID_ARG;
        break;
    }

    if (err != ESP_OK) {
        return err;
    }

    write_line("HTTP_DONE\n", context);
    return ESP_OK;
}