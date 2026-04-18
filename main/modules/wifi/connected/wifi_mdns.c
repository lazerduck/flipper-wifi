#include "modules/wifi/connected/wifi_mdns.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define WIFI_MDNS_RESULT_LINE_MAX_LENGTH 256
#define WIFI_MDNS_HOSTNAME_MAX_LENGTH 128
#define WIFI_MDNS_MAX_RESULTS 4

typedef struct {
    char address[INET6_ADDRSTRLEN];
    int family;
} wifi_mdns_result_entry_t;

static bool hostname_has_dot(const char *host)
{
    return host != NULL && strchr(host, '.') != NULL;
}

static esp_err_t normalize_hostname(const char *host, char *normalized_host, size_t normalized_host_size)
{
    int written;

    if (host == NULL || normalized_host == NULL || normalized_host_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (host[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (hostname_has_dot(host)) {
        written = snprintf(normalized_host, normalized_host_size, "%s", host);
    } else {
        written = snprintf(normalized_host, normalized_host_size, "%s.local", host);
    }

    if (written < 0 || (size_t)written >= normalized_host_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static const char *address_family_to_string(int family)
{
    switch (family) {
    case AF_INET:
        return "IPV4";
    case AF_INET6:
        return "IPV6";
    default:
        return "UNKNOWN";
    }
}

static bool format_sockaddr_address(const struct sockaddr *address, char *buffer, size_t buffer_size)
{
    const void *raw_address = NULL;

    if (address == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }

    if (address->sa_family == AF_INET) {
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)address;
        raw_address = &ipv4->sin_addr;
    } else if (address->sa_family == AF_INET6) {
        const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *)address;
        raw_address = &ipv6->sin6_addr;
    } else {
        return false;
    }

    return inet_ntop(address->sa_family, raw_address, buffer, buffer_size) != NULL;
}

static bool address_already_seen(const wifi_mdns_result_entry_t *entries, size_t count, const char *address)
{
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(entries[index].address, address) == 0) {
            return true;
        }
    }

    return false;
}

esp_err_t wifi_mdns_query_hostname(const char *host, wifi_mdns_result_writer_t write_line, void *context)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *result = NULL;
    char normalized_host[WIFI_MDNS_HOSTNAME_MAX_LENGTH];
    char line[WIFI_MDNS_RESULT_LINE_MAX_LENGTH];
    wifi_mdns_result_entry_t seen_results[WIFI_MDNS_MAX_RESULTS];
    size_t result_count = 0;
    size_t truncated_count = 0;
    int getaddrinfo_result;
    esp_err_t err;

    memset(&hints, 0, sizeof(hints));
    memset(normalized_host, 0, sizeof(normalized_host));
    memset(seen_results, 0, sizeof(seen_results));

    err = normalize_hostname(host, normalized_host, sizeof(normalized_host));
    if (err != ESP_OK) {
        return err;
    }

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo_result = getaddrinfo(normalized_host, NULL, &hints, &results);
    if (getaddrinfo_result != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    for (result = results; result != NULL; result = result->ai_next) {
        char address[INET6_ADDRSTRLEN];

        memset(address, 0, sizeof(address));
        if (!format_sockaddr_address(result->ai_addr, address, sizeof(address))) {
            continue;
        }

        if (address_already_seen(seen_results, result_count, address)) {
            continue;
        }

        if (result_count < WIFI_MDNS_MAX_RESULTS) {
            snprintf(seen_results[result_count].address, sizeof(seen_results[result_count].address), "%s", address);
            seen_results[result_count].family = result->ai_family;
            ++result_count;
        } else {
            ++truncated_count;
        }
    }

    if (result_count == 0) {
        freeaddrinfo(results);
        return ESP_ERR_NOT_FOUND;
    }

    if (write_line != NULL) {
        snprintf(line, sizeof(line), "MDNS_COUNT %u\n", (unsigned int)result_count);
        write_line(line, context);

        for (size_t index = 0; index < result_count; ++index) {
            const char *family = address_family_to_string(seen_results[index].family);

            snprintf(
                line,
                sizeof(line),
                "MDNS host=%.*s addr=%.*s family=%.*s\n",
                (int)(sizeof(normalized_host) - 1U),
                normalized_host,
                (int)(sizeof(seen_results[index].address) - 1U),
                seen_results[index].address,
                8,
                family);
            write_line(line, context);
        }

        if (truncated_count > 0) {
            snprintf(line, sizeof(line), "MDNS_TRUNCATED %u\n", (unsigned int)truncated_count);
            write_line(line, context);
        }

        write_line("MDNS_DONE\n", context);
    }

    freeaddrinfo(results);

    return ESP_OK;
}
