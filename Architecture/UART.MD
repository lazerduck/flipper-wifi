# UART Protocol

## Overview

Communication between the Flipper Zero and the ESP32 uses UART1 at 115200 baud, 8N1.

Each message is a single line terminated by `\n` (LF, 0x0A). The Flipper sends commands; the ESP32 replies with one or more response lines. Commands are processed one at a time — the Flipper should wait for a final response (`S`, `E`, `END`, or `STOPPED`) before sending the next command. The one exception is `SYS_STOP`, which may be sent at any time to interrupt a running operation (see [Interrupting a command](#interrupting-a-command)).

---

## Command Format

```
COMMAND_NAME [key="value" ...]\n
```

- Command names are `UPPER_SNAKE_CASE`, namespaced by module: `WIFI_`, `BLE_`, `ZIGBEE_`, `SD_`, `LED_`, `SYS_`
- Arguments are `key="value"` pairs separated by single spaces
- All values are quoted strings on the wire; the receiver casts to the expected type based on context
- Arrays are pipe-separated inside a single quoted value: `channels="1|6|11"`
- `\n` always terminates a command; values must not contain literal newlines

### Examples

```
PING\n
SYS_TIME_SET epoch="1747353600"\n
WIFI_CONNECT ssid="MyNetwork" password="hunter2"\n
WIFI_PROMISCUOUS_SCAN channels="1|6|11" duration="30"\n
BLE_SCAN duration="10"\n
LED_SET r="0" g="0" b="255"\n
```

---

## Response Format

Every command produces at least one response line. There are three response types:

### Immediate success

```
S [key="value" ...]\n
```

Used when a command completes and returns a fixed result. Arguments are optional.

```
S\n
S msg="PONG"\n
S ip="192.168.1.42" mac="AA:BB:CC:DD:EE:FF"\n
```

### Streaming success

Used for commands that return multiple records (e.g. scan results). The ESP32 emits a stream of `DATA` lines, then a closing `END`.

```
S STREAM\n
DATA key="value" key2="value2"\n
DATA key="value" key2="value2"\n
END\n
```

If an error occurs mid-stream, `END` is replaced by `E`:

```
S STREAM\n
DATA ...\n
E code="ERR_WIFI_TIMEOUT"\n
```

If the stream was cut short by `SYS_STOP`, `END` is replaced by `STOPPED`:

```
S STREAM\n
DATA ...\n
STOPPED\n
```

This lets the Flipper distinguish "scan finished" from "user cancelled" and display accordingly.

### Error

```
E code="ERR_CODE"\n
```

Error codes are `UPPER_SNAKE_CASE` strings scoped to the failing command. See the command reference for per-command codes. A small set of general codes apply everywhere:

| Code | Meaning |
|------|---------|
| `ERR_UNKNOWN_CMD` | Command name not recognised |
| `ERR_MISSING_ARG` | A required argument was absent |
| `ERR_WRONG_MODE` | Command not valid in the current radio mode |
| `ERR_BUSY` | A previous command is still running |

### Progress events

There are no separate progress events. Commands either complete immediately (`S`/`E`) or open a stream (`S STREAM`) and push `DATA` lines as results arrive. The Flipper can display a "scanning..." state simply from knowing a stream is open.

---

## Interrupting a command

`SYS_STOP` may be sent at any time — including while a streaming response or continuous operation is in progress. The Flipper should send this whenever the user cancels or navigates back, without needing to know what is currently running.

```
SYS_STOP\n
```

The ESP32 will:
1. Abort the active operation
2. If a stream was open, emit `STOPPED\n` to close it
3. That `STOPPED` line is the only acknowledgement — `SYS_STOP` has no response of its own

If nothing is running when `SYS_STOP` arrives, the ESP32 silently ignores it (no response).

### Terminal lines summary

| Line | Meaning |
|------|---------|
| `S [...]` | Command succeeded (immediate) |
| `S STREAM` | Streaming response starting |
| `DATA [...]` | One record in a stream |
| `END` | Stream finished naturally |
| `STOPPED` | Stream cut short by `SYS_STOP` |
| `E code="..."` | Error (or mid-stream failure) |

```
S STREAM\n
DATA channel="1" count="4"\n
DATA channel="6" count="18"\n
END\n
```

---

## Value Types

| Type | Wire format | Example |
|------|-------------|---------|
| String | `key="value"` | `ssid="MyNetwork"` |
| Integer | `key="42"` | `channel="6"` |
| Float | `key="-72.5"` | `rssi="-72.5"` |
| Boolean | `key="true"` / `key="false"` | `open="true"` |
| Array | pipe-separated inside quotes | `channels="1|6|11"` |

Single-value strings may contain any characters including commas and spaces. If an array element must contain a literal `|`, escape it as `\|` (rare in practice — channel numbers, MACs, and IPs never require this).

Inner double-quotes should be escaped as `\"`.

---

## Line Termination

The ESP32 sends `\n` only. It accepts both `\n` and `\r\n` from the Flipper for tolerance. Values must not contain literal newline characters.

---

## Command Reference

### System

The ESP32 boots in `IDLE` mode with no radio initialised. The typical Flipper session flow is: `PING` to confirm ready → `SYS_MODE_SET` to init the chosen radio → radio commands → `SYS_RESTART` when returning to the main menu.

| Command | Arguments | Success response | Error codes |
|---------|-----------|-----------------|-------------|
| `PING` | — | `S msg="PONG"` | — |
| `SYS_MODE_GET` | — | `S mode="IDLE\|WIFI\|BLE\|ZIGBEE"` | — |
| `SYS_MODE_SET` | `mode="WIFI\|BLE\|ZIGBEE"` | `S` | `ERR_MISSING_ARG`, `ERR_MODE_INIT_FAIL` |
| `SYS_TIME_SET` | `epoch="<unix timestamp>"` | `S` | `ERR_MISSING_ARG` |
| `SYS_STOP` | — | `STOPPED` (if operation was active), silent otherwise | — |
| `SYS_RESTART` | — | *(device restarts, no response)* | — |

### Wi-Fi

| Command | Arguments | Success response | Error codes |
|---------|-----------|-----------------|-------------|
| `WIFI_SCAN_SSIDS` | — | `S STREAM` → `DATA ssid="..." rssi="-70" open="true"` | `ERR_WRONG_MODE` |
| `WIFI_CONNECT` | `ssid="..." password="..."` *(optional)* | `S ip="..." mac="..."` | `ERR_WIFI_NOT_FOUND`, `ERR_WIFI_AUTH`, `ERR_WIFI_TIMEOUT` |
| `WIFI_PROMISCUOUS_SCAN` | `channels="1|6|11" duration="30"` | `S STREAM` → `DATA channel="6" count="42"` | `ERR_WRONG_MODE` |
| `WIFI_DEVICE_SCAN` | `channel="6" duration="30"` | `S STREAM` → `DATA mac="..." type="AP" rssi="-65"` | `ERR_WRONG_MODE` |
| `WIFI_IP_SWEEP` | `subnet="192.168.1.0/24"` | `S STREAM` → `DATA ip="..." port_22="true" hostname="..."` | `ERR_WIFI_NOT_CONNECTED` |
| `WIFI_BEACON_SPAM` | `ssids="Net One|Net Two|Net Three"` | `S` (running silently until `SYS_STOP` → `STOPPED`) | `ERR_WRONG_MODE` |

### BLE

| Command | Arguments | Success response | Error codes |
|---------|-----------|-----------------|-------------|
| `BLE_SCAN` | `duration="10"` | `S STREAM` → `DATA mac="..." name="..." rssi="-55"` | `ERR_WRONG_MODE` |
| `BLE_GATT_INSPECT` | `mac="AA:BB:CC:DD:EE:FF"` | `S STREAM` → `DATA uuid="..." value="..."` | `ERR_BLE_CONNECT`, `ERR_BLE_TIMEOUT` |
| `BLE_RSSI_TRACK` | `mac="AA:BB:CC:DD:EE:FF"` | `S STREAM` → `DATA rssi="-60"` until `SYS_STOP` → `STOPPED` | `ERR_BLE_NOT_FOUND` |
| `BLE_DEVICE_SAVE` | `mac="..." name="..." notes="..."` | `S` | `ERR_MISSING_ARG`, `ERR_SD_NOT_MOUNTED`, `ERR_SD_WRITE_FAIL` |

### ZigBee

| Command | Arguments | Success response | Error codes |
|---------|-----------|-----------------|-------------|
| `ZIGBEE_CHANNEL_SCAN` | `channels="11|15|20|25" duration="30"` | `S STREAM` → `DATA channel="15" packets="12"` | `ERR_WRONG_MODE`, `ERR_ZIGBEE_INIT` |
| `ZIGBEE_LISTEN` | `channel="15"` | `S STREAM` → `DATA` frames until `SYS_STOP` → `STOPPED` | `ERR_WRONG_MODE`, `ERR_ZIGBEE_INIT` |

### SD Card

| Command | Arguments | Success response | Error codes |
|---------|-----------|-----------------|-------------|
| `SD_STATUS` | — | `S present="true" mounted="true" free_kb="14200"` | — |
| `SD_LIST` | `path="/"` | `S STREAM` → `DATA name="log.txt" size="1024" dir="false"` | `ERR_SD_NOT_MOUNTED`, `ERR_SD_PATH_NOT_FOUND` |
| `SD_FORMAT` | — | `S` | `ERR_SD_NOT_MOUNTED`, `ERR_SD_WRITE_FAIL` |

### LED

The LED is managed internally by the firmware to reflect the current mode and activity. The Flipper can override it directly with an explicit RGB value — e.g. for a custom colour or a user-triggered effect. The internal LED logic resumes on the next mode change or action.

| Command | Arguments | Success response | Error codes |
|---------|-----------|-----------------|-------------|
| `LED_SET` | `r="0-255" g="0-255" b="0-255"` | `S` | `ERR_MISSING_ARG` |

### Settings

Settings are persisted to the SD card. If the SD card is absent, reads return defaults and writes return `ERR_SD_NOT_MOUNTED`.

| Command | Arguments | Success response | Error codes |
|---------|-----------|-----------------|-------------|
| `SETTINGS_GET` | `key="..."` | `S value="..."` | `ERR_MISSING_ARG`, `ERR_SETTINGS_KEY_NOT_FOUND` |
| `SETTINGS_SET` | `key="..." value="..."` | `S` | `ERR_MISSING_ARG`, `ERR_SD_NOT_MOUNTED`, `ERR_SD_WRITE_FAIL` |
