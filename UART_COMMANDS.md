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
| `WIFI READ_MDNS` | Stub | Reserved, not implemented |
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
ERR USAGE WIFI <SCAN|CONNECT|STATUS|READ_MDNS|DISCONNECT>
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
- Because of that parser, SSIDs and passwords cannot contain spaces in the current implementation.

Examples:

```text
WIFI CONNECT ssid=OfficeWiFi psw=correcthorsebatterystaple
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

### `WIFI READ_MDNS`

Reserved command, currently not implemented.

Request:

```text
WIFI READ_MDNS
```

If Wi-Fi is not connected with an IP address:

```text
ERR WIFI_NOT_CONNECTED
```

If Wi-Fi is connected, the current implementation still returns:

```text
ERR MDNS_NOT_IMPLEMENTED
```

Other possible responses:

```text
ERR USAGE WIFI READ_MDNS
ERR WIFI_STATUS_FAILED
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
- There is no escaping or quoting support in command arguments.
- The firmware suppresses ESP log output, so UART traffic should mostly contain only command responses.
- Wi-Fi mode is station-only.
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