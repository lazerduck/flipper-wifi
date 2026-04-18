# UART Command Reference

This firmware exposes a simple ASCII command interface over a dedicated UART.

## UART transport

- Interface: UART, 8 data bits, no parity, 1 stop bit
- Current build configuration:
  - UART port: `1`
  - Baud rate: `115200`
  - TX pin: `GPIO1`
  - RX pin: `GPIO0`
- Input framing: one command per line, terminated by `\n`
- Output framing: one or more newline-terminated text lines
- Maximum command length: 255 characters before the newline

## Session behavior

- On boot, the device sends:

```text
READY
```

- Commands are case-sensitive.
- Commands are matched as plain text. Use the uppercase forms shown below.
- The parser does not strip `\r`, so use LF line endings rather than CRLF when possible.
- Named argument values may be unquoted or wrapped in `"..."`.
- Quoted values may contain spaces. Inside quoted values, use `\"` for a literal quote and `\\` for a literal backslash.
- If a line exceeds the command buffer, the firmware resets the buffer and replies with:

```text
ERR COMMAND_TOO_LONG
```

- Unknown top-level commands reply with:

```text
ERR UNKNOWN_COMMAND
```

## Top-level commands

| Command | Status | Purpose |
| --- | --- | --- |
| `PING` | Implemented | Link test / keepalive |
| `WIFI SCAN` | Implemented | Scan nearby APs |
| `WIFI CONNECT ssid=<ssid> psw=<password>` | Implemented | Start STA connection |
| `WIFI STATUS` | Implemented | Read current STA state |
| `WIFI DISCONNECT` | Implemented | Disconnect from current AP |
| `WIFI DISCOVER` | Implemented | Probe the current IPv4 subnet for reachable hosts |
| `WIFI READ_MDNS host=<hostname>` | Implemented | Resolve an mDNS hostname on the connected network |
| `SEND <payload>` | Stub | Reserved, not implemented |
| `QUERY <request>` | Stub | Reserved, not implemented |

## Command details

### `PING`

Checks that the UART command path is alive.

Request:

```text
PING
```

Response:

```text
PONG
```

### `WIFI`

Entering `WIFI` with no subcommand returns usage:

```text
ERR USAGE WIFI <SCAN|CONNECT|STATUS|READ_MDNS|DISCONNECT|DISCOVER>
```

If the subcommand is not recognized:

```text
ERR UNKNOWN_WIFI_COMMAND
```

### `WIFI SCAN`

Performs a blocking Wi-Fi station scan and returns visible APs.

Request:

```text
WIFI SCAN
```

Successful response format:

```text
SCAN_COUNT <total>
AP <ssid> RSSI <rssi> AUTH <auth> CH <channel>
AP <ssid> RSSI <rssi> AUTH <auth> CH <channel>
...
SCAN_DONE
```

Notes:

- The scan is blocking inside the command handler. The caller should wait for `SCAN_DONE`.
- Hidden networks are included in the scan.
- At most 20 AP detail lines are returned.
- If more than 20 APs are found, an extra line is emitted:

```text
SCAN_TRUNCATED <remaining_count>
```

- Authentication strings currently emitted are: `OPEN`, `WEP`, `WPA`, `WPA2`, `WPA_WPA2`, `WPA3`, `WPA2_WPA3`, or `UNKNOWN`.

Usage error:

```text
ERR USAGE WIFI SCAN
```

Execution failure:

```text
ERR SCAN_FAILED
```

Example:

```text
WIFI SCAN
SCAN_COUNT 3
AP OfficeWiFi RSSI -54 AUTH WPA2 CH 6
AP Guest RSSI -71 AUTH OPEN CH 1
AP LabNet RSSI -82 AUTH WPA3 CH 11
SCAN_DONE
```

### `WIFI CONNECT ssid=<ssid> psw=<password>`

Starts a station connection attempt using the provided credentials.

Request:

```text
WIFI CONNECT ssid=<ssid> psw=<password>
```

Successful immediate response:

```text
WIFI CONNECTING
```

Usage error:

```text
ERR USAGE WIFI CONNECT ssid=<ssid> psw=<password>
```

Execution failure:

```text
ERR WIFI_CONNECT_FAILED
```

Notes:

- This command only starts the connection attempt. It does not wait for DHCP or report final success over UART.
- To observe completion, poll `WIFI STATUS`.
- `ssid` is required and must be non-empty.
- `psw` is required by the parser, even for open networks.
- Arguments are parsed as space-separated `key=value` tokens.
- Values without spaces may be sent unquoted.
- Values that contain spaces must be wrapped in `"..."`.
- Inside quoted values, use `\"` for a literal quote and `\\` for a literal backslash.

Examples:

```text
WIFI CONNECT ssid=OfficeWiFi psw=correcthorsebatterystaple
WIFI CONNECTING
```

```text
WIFI CONNECT ssid="Office WiFi" psw="correct horse battery staple"
WIFI CONNECTING
```

```text
WIFI STATUS
WIFI STATUS state=CONNECTED connected=yes ssid=OfficeWiFi reason=0
```

### `WIFI STATUS`

Returns the current Wi-Fi station state snapshot.

Request:

```text
WIFI STATUS
```

Successful response format:

```text
WIFI STATUS state=<IDLE|SCANNING|CONNECTING|CONNECTED> connected=<yes|no> ssid=<ssid|-> reason=<code>
```

Field meanings:

- `state`: internal station state
- `connected`: `yes` only after the station has an IP address
- `ssid`: currently connected or target SSID, `-` when none is known
- `reason`: last disconnect reason code from the ESP-IDF Wi-Fi stack, `0` when none is recorded

Usage error:

```text
ERR USAGE WIFI STATUS
```

Execution failure:

```text
ERR WIFI_STATUS_FAILED
```

Examples:

```text
WIFI STATUS
WIFI STATUS state=IDLE connected=no ssid=- reason=0
```

```text
WIFI STATUS
WIFI STATUS state=CONNECTING connected=no ssid=OfficeWiFi reason=0
```

```text
WIFI STATUS
WIFI STATUS state=CONNECTED connected=yes ssid=OfficeWiFi reason=0
```

### `WIFI DISCONNECT`

Requests disconnection from the current AP.

Request:

```text
WIFI DISCONNECT
```

Successful response:

```text
WIFI DISCONNECTING
```

Usage error:

```text
ERR USAGE WIFI DISCONNECT
```

Execution failure:

```text
ERR WIFI_DISCONNECT_FAILED
```

Notes:

- If the station is already disconnected, the command still returns `WIFI DISCONNECTING`.
- As with connect, this does not stream a later completion event. Poll `WIFI STATUS` if you need confirmation.

### `WIFI DISCOVER`

Probes the current connected IPv4 subnet for reachable hosts.

Request:

```text
WIFI DISCOVER
```

Successful response format:

```text
DISCOVER_NETWORK subnet=<network>/<prefix> self=<local_ip>
DISCOVER_FOUND ip=<ip> host=<hostname|-> rtt_ms=<round_trip_ms>
DISCOVER_FOUND ip=<ip> host=<hostname|-> rtt_ms=<round_trip_ms>
...
DISCOVER_DONE scanned=<count> found=<count> duration_ms=<total_ms>
```

Notes:

- The discovery scan is blocking inside the command handler.
- The current implementation probes each usable host in the connected IPv4 subnet with one ICMP echo request.
- The current implementation probes up to 4 hosts in parallel per batch to stay within typical ESP-IDF socket limits.
- Results are streamed only for hosts that respond to the ping.
- The current implementation does not perform hostname resolution yet, so `host=-` is expected for all results.
- The current implementation supports subnets with at most 254 usable host addresses. Larger subnets return an error.

If Wi-Fi is not connected with an IP address:

```text
ERR WIFI_NOT_CONNECTED
```

Other possible responses:

```text
ERR USAGE WIFI DISCOVER
ERR WIFI_STATUS_FAILED
ERR DISCOVER_NOT_READY
ERR DISCOVER_SUBNET_TOO_LARGE
ERR DISCOVER_TIMEOUT
ERR DISCOVER_IP_INFO_FAILED
ERR DISCOVER_NO_MEM
ERR DISCOVER_BUSY
ERR DISCOVER_FAILED
```

Notes:

- `ERR DISCOVER_BUSY` now specifically means another discovery is already running.
- `ERR DISCOVER_IP_INFO_FAILED` means the firmware could not read the current station IPv4 info from `esp_netif`.
- `ERR DISCOVER_NO_MEM` means the firmware could not allocate the resources needed to run discovery, typically a ping session or completion semaphore.
- Discovery diagnostics are also emitted through the ESP-IDF log console under the `wifi_discovery` tag. In the current configuration that goes to the ESP console on UART0, with USB Serial/JTAG enabled as a secondary console.

Example:

```text
WIFI DISCOVER
DISCOVER_NETWORK subnet=192.168.1.0/24 self=192.168.1.42
DISCOVER_FOUND ip=192.168.1.1 host=- rtt_ms=2
DISCOVER_FOUND ip=192.168.1.44 host=- rtt_ms=6
DISCOVER_FOUND ip=192.168.1.87 host=- rtt_ms=14
DISCOVER_DONE scanned=253 found=3 duration_ms=5412
```

### `WIFI READ_MDNS host=<hostname>`

Resolves an mDNS hostname on the connected network.

Request:

```text
WIFI READ_MDNS host=<hostname>
```

Successful response format:

```text
MDNS_COUNT <count>
MDNS host=<hostname.local> addr=<ip> family=<IPV4|IPV6>
...
MDNS_DONE
```

Notes:

- If `host` does not contain a dot, the firmware automatically appends `.local` before querying.
- At most 4 unique IP addresses are returned.
- If more than 4 unique answers are found, an extra line is emitted:
- If the hostname contains spaces, wrap it in `"..."`.

```text
MDNS_TRUNCATED <remaining_count>
```

If Wi-Fi is not connected with an IP address:

```text
ERR WIFI_NOT_CONNECTED
```

If the hostname cannot be resolved:

```text
ERR MDNS_NOT_FOUND
```

Other possible responses:

```text
ERR USAGE WIFI READ_MDNS host=<hostname>
ERR WIFI_STATUS_FAILED
ERR MDNS_QUERY_FAILED
```

Example:

```text
WIFI READ_MDNS host=printer
MDNS_COUNT 1
MDNS host=printer.local addr=192.168.1.44 family=IPV4
MDNS_DONE
```

```text
WIFI READ_MDNS host="printer room"
MDNS_COUNT 1
MDNS host=printer room.local addr=192.168.1.44 family=IPV4
MDNS_DONE
```

### `SEND <payload>`

Reserved top-level command, currently not implemented.

Request without payload:

```text
SEND
```

Response:

```text
ERR USAGE SEND <payload>
```

Any payload currently returns:

```text
ERR SEND_NOT_IMPLEMENTED
```

### `QUERY <request>`

Reserved top-level command, currently not implemented.

Request without argument:

```text
QUERY
```

Response:

```text
ERR USAGE QUERY <request>
```

Any request payload currently returns:

```text
ERR QUERY_NOT_IMPLEMENTED
```

## Practical usage notes

- There is no `HELP` command.
- Named `key=value` arguments support optional double-quoted values.
- The firmware suppresses ESP log output, so UART traffic should mostly contain only command responses.
- Wi-Fi mode is station-only.
- `WIFI DISCOVER` currently targets the connected IPv4 subnet and is optimized for typical `/24` home LANs.
- This interface is request/response text, not a framed binary protocol.

## Minimal test sequence

```text
READY
PING
PONG
WIFI STATUS
WIFI STATUS state=IDLE connected=no ssid=- reason=0
WIFI SCAN
SCAN_COUNT 2
AP MyAP RSSI -61 AUTH WPA2 CH 6
AP Guest RSSI -80 AUTH OPEN CH 11
SCAN_DONE
WIFI CONNECT ssid=MyAP psw=secret123
WIFI CONNECTING
WIFI STATUS
WIFI STATUS state=CONNECTED connected=yes ssid=MyAP reason=0
```