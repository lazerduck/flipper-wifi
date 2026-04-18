#include "modules/wifi/wifi_discovery.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"

#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/sockets.h"

#define WIFI_DISCOVERY_IFKEY "WIFI_STA_DEF"
#define WIFI_DISCOVERY_MAX_PARALLEL_PINGS 4
#define WIFI_DISCOVERY_MAX_HOSTS 254U
#define WIFI_DISCOVERY_LINE_MAX_LENGTH 192
#define WIFI_DISCOVERY_PING_TIMEOUT_MS 250U
#define WIFI_DISCOVERY_PING_DATA_SIZE 16U
#define WIFI_DISCOVERY_RECV_BUFFER_SIZE 96U

static const char *WIFI_DISCOVERY_TAG = "wifi_discovery";

typedef struct {
    uint32_t host_ip;
    uint16_t sequence_number;
    int64_t sent_at_us;
    uint32_t round_trip_ms;
    bool responded;
} wifi_discovery_target_t;

static portMUX_TYPE s_discovery_guard = portMUX_INITIALIZER_UNLOCKED;
static bool s_discovery_in_progress = false;

static bool wifi_discovery_try_begin(void)
{
    bool acquired = false;

    portENTER_CRITICAL(&s_discovery_guard);
    if (!s_discovery_in_progress) {
        s_discovery_in_progress = true;
        acquired = true;
    }
    portEXIT_CRITICAL(&s_discovery_guard);

    return acquired;
}

static void wifi_discovery_finish(void)
{
    portENTER_CRITICAL(&s_discovery_guard);
    s_discovery_in_progress = false;
    portEXIT_CRITICAL(&s_discovery_guard);
}

static bool wifi_discovery_netmask_is_contiguous(uint32_t netmask)
{
    uint32_t inverted_mask;

    if (netmask == 0U) {
        return false;
    }

    inverted_mask = ~netmask;
    return (inverted_mask & (inverted_mask + 1U)) == 0U;
}

static uint32_t wifi_discovery_netmask_to_prefix(uint32_t netmask)
{
    uint32_t prefix = 0;

    while ((netmask & 0x80000000U) != 0U) {
        ++prefix;
        netmask <<= 1U;
    }

    return prefix;
}

static void wifi_discovery_format_ipv4(uint32_t host_ip, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    snprintf(
        buffer,
        buffer_size,
        "%u.%u.%u.%u",
        (unsigned int)((host_ip >> 24U) & 0xFFU),
        (unsigned int)((host_ip >> 16U) & 0xFFU),
        (unsigned int)((host_ip >> 8U) & 0xFFU),
        (unsigned int)(host_ip & 0xFFU));
}

static void wifi_discovery_write_network_line(
    wifi_discovery_result_writer_t write_line,
    void *context,
    uint32_t network_ip,
    uint32_t prefix_length,
    uint32_t self_ip)
{
    char network_string[16];
    char self_string[16];
    char line[WIFI_DISCOVERY_LINE_MAX_LENGTH];

    if (write_line == NULL) {
        return;
    }

    wifi_discovery_format_ipv4(network_ip, network_string, sizeof(network_string));
    wifi_discovery_format_ipv4(self_ip, self_string, sizeof(self_string));

    snprintf(
        line,
        sizeof(line),
        "DISCOVER_NETWORK subnet=%s/%u self=%s\n",
        network_string,
        (unsigned int)prefix_length,
        self_string);
    write_line(line, context);
}

static void wifi_discovery_write_found_line(
    wifi_discovery_result_writer_t write_line,
    void *context,
    const wifi_discovery_target_t *target)
{
    char ip_string[16];
    char line[WIFI_DISCOVERY_LINE_MAX_LENGTH];

    if (write_line == NULL || target == NULL) {
        return;
    }

    wifi_discovery_format_ipv4(target->host_ip, ip_string, sizeof(ip_string));

    snprintf(
        line,
        sizeof(line),
        "DISCOVER_FOUND ip=%s host=%s rtt_ms=%u\n",
        ip_string,
        "-",
        (unsigned int)target->round_trip_ms);
    write_line(line, context);
}

static esp_err_t wifi_discovery_open_ping_socket(int *socket_fd)
{
    int ping_socket;
    struct timeval timeout = {
        .tv_sec = WIFI_DISCOVERY_PING_TIMEOUT_MS / 1000U,
        .tv_usec = (WIFI_DISCOVERY_PING_TIMEOUT_MS % 1000U) * 1000U,
    };

    if (socket_fd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ping_socket = socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);
    if (ping_socket < 0) {
        int socket_errno = errno;

        ESP_LOGE(
            WIFI_DISCOVERY_TAG,
            "failed to create raw ICMP socket: errno=%d",
            socket_errno);
        if (socket_errno == ENOMEM || socket_errno == ENOBUFS) {
            return ESP_ERR_NO_MEM;
        }

        return ESP_FAIL;
    }

    if (setsockopt(ping_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        int socket_errno = errno;

        ESP_LOGE(
            WIFI_DISCOVERY_TAG,
            "failed to configure ICMP socket timeout: errno=%d",
            socket_errno);
        close(ping_socket);
        return ESP_FAIL;
    }

    *socket_fd = ping_socket;
    return ESP_OK;
}

static esp_err_t wifi_discovery_send_ping(int socket_fd, uint16_t ping_id, wifi_discovery_target_t *target)
{
    struct sockaddr_in destination_address;
    struct {
        struct icmp_echo_hdr header;
        uint8_t payload[WIFI_DISCOVERY_PING_DATA_SIZE];
    } packet;
    ssize_t sent;

    if (target == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&destination_address, 0, sizeof(destination_address));
    memset(&packet, 0, sizeof(packet));

    destination_address.sin_family = AF_INET;
    destination_address.sin_addr.s_addr = esp_netif_htonl(target->host_ip);

    packet.header.type = ICMP_ECHO;
    packet.header.code = 0;
    packet.header.id = ping_id;
    packet.header.seqno = target->sequence_number;
    for (size_t index = 0; index < sizeof(packet.payload); ++index) {
        packet.payload[index] = (uint8_t)('A' + (index % 26U));
    }
    packet.header.chksum = inet_chksum(&packet, sizeof(packet));

    sent = sendto(
        socket_fd,
        &packet,
        sizeof(packet),
        0,
        (const struct sockaddr *)&destination_address,
        sizeof(destination_address));
    if (sent != (ssize_t)sizeof(packet)) {
        int socket_errno = errno;
        char host_string[16];

        wifi_discovery_format_ipv4(target->host_ip, host_string, sizeof(host_string));
        ESP_LOGE(
            WIFI_DISCOVERY_TAG,
            "failed to send ICMP echo to %s: errno=%d",
            host_string,
            socket_errno);
        if (socket_errno == ENOMEM || socket_errno == ENOBUFS) {
            return ESP_ERR_NO_MEM;
        }

        return ESP_FAIL;
    }

    target->sent_at_us = esp_timer_get_time();
    return ESP_OK;
}

static wifi_discovery_target_t *wifi_discovery_find_target_by_sequence(
    wifi_discovery_target_t *targets,
    size_t count,
    uint16_t sequence_number)
{
    for (size_t index = 0; index < count; ++index) {
        if (targets[index].sequence_number == sequence_number) {
            return &targets[index];
        }
    }

    return NULL;
}

static esp_err_t wifi_discovery_receive_batch(
    int socket_fd,
    uint16_t ping_id,
    wifi_discovery_target_t *targets,
    size_t count)
{
    int64_t deadline_us;
    uint32_t responses_remaining = (uint32_t)count;

    if (targets == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    deadline_us = esp_timer_get_time() + ((int64_t)WIFI_DISCOVERY_PING_TIMEOUT_MS * 1000LL);
    while (responses_remaining > 0 && esp_timer_get_time() < deadline_us) {
        uint8_t recv_buffer[WIFI_DISCOVERY_RECV_BUFFER_SIZE];
        struct sockaddr_storage source_address;
        socklen_t source_address_length = sizeof(source_address);
        ssize_t received_length;

        memset(recv_buffer, 0, sizeof(recv_buffer));
        memset(&source_address, 0, sizeof(source_address));

        received_length = recvfrom(
            socket_fd,
            recv_buffer,
            sizeof(recv_buffer),
            0,
            (struct sockaddr *)&source_address,
            &source_address_length);
        if (received_length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            ESP_LOGE(
                WIFI_DISCOVERY_TAG,
                "ICMP receive failed: errno=%d",
                errno);
            return ESP_FAIL;
        }

        if (source_address.ss_family == AF_INET && received_length >= (ssize_t)(sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr))) {
            const struct ip_hdr *ip_header = (const struct ip_hdr *)recv_buffer;
            const struct icmp_echo_hdr *echo_reply =
                (const struct icmp_echo_hdr *)(recv_buffer + IPH_HL_BYTES(ip_header));

            if (echo_reply->type == ICMP_ER && echo_reply->id == ping_id) {
                wifi_discovery_target_t *target =
                    wifi_discovery_find_target_by_sequence(targets, count, echo_reply->seqno);
                if (target != NULL && !target->responded) {
                    int64_t elapsed_us = esp_timer_get_time() - target->sent_at_us;

                    target->responded = true;
                    target->round_trip_ms = (uint32_t)(elapsed_us / 1000LL);
                    --responses_remaining;
                }
            }
        }
    }

    return ESP_OK;
}

esp_err_t wifi_discovery_scan_subnet(wifi_discovery_result_writer_t write_line, void *context)
{
    esp_netif_t *station_netif;
    esp_netif_ip_info_t ip_info;
    wifi_discovery_target_t targets[WIFI_DISCOVERY_MAX_PARALLEL_PINGS];
    int ping_socket = -1;
    esp_err_t result = ESP_OK;
    uint32_t ip;
    uint32_t netmask;
    uint32_t network;
    uint32_t broadcast;
    uint32_t host_count;
    uint32_t prefix_length;
    uint32_t scanned_count = 0;
    uint32_t found_count = 0;
    uint32_t current_host;
    uint16_t ping_id;
    uint16_t next_sequence_number = 1U;
    int64_t started_at_us;

    memset(targets, 0, sizeof(targets));

    if (!wifi_discovery_try_begin()) {
        ESP_LOGW(WIFI_DISCOVERY_TAG, "discovery request rejected: already in progress");
        return WIFI_DISCOVERY_ERR_BUSY;
    }

    station_netif = esp_netif_get_handle_from_ifkey(WIFI_DISCOVERY_IFKEY);
    if (station_netif == NULL) {
        ESP_LOGW(WIFI_DISCOVERY_TAG, "station netif %s not available", WIFI_DISCOVERY_IFKEY);
        result = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    result = esp_netif_get_ip_info(station_netif, &ip_info);
    if (result != ESP_OK) {
        ESP_LOGE(
            WIFI_DISCOVERY_TAG,
            "esp_netif_get_ip_info failed: %s (%d)",
            esp_err_to_name(result),
            (int)result);
        result = WIFI_DISCOVERY_ERR_IP_INFO_FAILED;
        goto cleanup;
    }

    ip = esp_netif_htonl(ip_info.ip.addr);
    netmask = esp_netif_htonl(ip_info.netmask.addr);
    if (ip == 0U || !wifi_discovery_netmask_is_contiguous(netmask)) {
        ESP_LOGW(WIFI_DISCOVERY_TAG, "station IPv4 settings are not ready or netmask is invalid");
        result = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    network = ip & netmask;
    broadcast = network | (~netmask);
    if (broadcast <= network + 1U) {
        ESP_LOGW(WIFI_DISCOVERY_TAG, "subnet has no usable host range");
        result = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    host_count = broadcast - network - 1U;
    if (host_count > WIFI_DISCOVERY_MAX_HOSTS) {
        ESP_LOGW(
            WIFI_DISCOVERY_TAG,
            "subnet too large for discovery: hosts=%u max=%u",
            (unsigned int)host_count,
            (unsigned int)WIFI_DISCOVERY_MAX_HOSTS);
        result = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    result = wifi_discovery_open_ping_socket(&ping_socket);
    if (result != ESP_OK) {
        goto cleanup;
    }

    ping_id = (uint16_t)(esp_timer_get_time() & 0xFFFFU);

    prefix_length = wifi_discovery_netmask_to_prefix(netmask);
    wifi_discovery_write_network_line(write_line, context, network, prefix_length, ip);

    {
        char network_string[16];
        char self_string[16];

        wifi_discovery_format_ipv4(network, network_string, sizeof(network_string));
        wifi_discovery_format_ipv4(ip, self_string, sizeof(self_string));
        ESP_LOGI(
            WIFI_DISCOVERY_TAG,
            "starting subnet discovery: subnet=%s/%u self=%s hosts=%u parallel=%u",
            network_string,
            (unsigned int)prefix_length,
            self_string,
            (unsigned int)host_count,
            (unsigned int)WIFI_DISCOVERY_MAX_PARALLEL_PINGS);
    }

    started_at_us = esp_timer_get_time();
    current_host = network + 1U;
    while (current_host < broadcast) {
        size_t batch_count = 0;

        memset(targets, 0, sizeof(targets));
        while (current_host < broadcast && batch_count < WIFI_DISCOVERY_MAX_PARALLEL_PINGS) {
            if (current_host == ip) {
                ++current_host;
                continue;
            }

            targets[batch_count].host_ip = current_host;
            targets[batch_count].sequence_number = next_sequence_number++;
            result = wifi_discovery_send_ping(ping_socket, ping_id, &targets[batch_count]);
            if (result != ESP_OK) {
                goto cleanup;
            }

            ++batch_count;
            ++scanned_count;
            ++current_host;
        }

        result = wifi_discovery_receive_batch(ping_socket, ping_id, targets, batch_count);
        if (result != ESP_OK) {
            goto cleanup;
        }

        for (size_t index = 0; index < batch_count; ++index) {
            if (targets[index].responded) {
                ++found_count;
                wifi_discovery_write_found_line(write_line, context, &targets[index]);
            }
        }
    }

    if (write_line != NULL) {
        char line[WIFI_DISCOVERY_LINE_MAX_LENGTH];
        uint32_t duration_ms = (uint32_t)((esp_timer_get_time() - started_at_us) / 1000LL);

        snprintf(
            line,
            sizeof(line),
            "DISCOVER_DONE scanned=%u found=%u duration_ms=%u\n",
            (unsigned int)scanned_count,
            (unsigned int)found_count,
            (unsigned int)duration_ms);
        write_line(line, context);

        ESP_LOGI(
            WIFI_DISCOVERY_TAG,
            "discovery complete: scanned=%u found=%u duration_ms=%u",
            (unsigned int)scanned_count,
            (unsigned int)found_count,
            (unsigned int)duration_ms);
    }

    result = ESP_OK;

cleanup:
    if (ping_socket >= 0) {
        close(ping_socket);
    }
    wifi_discovery_finish();
    return result;
}