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
| `BLE <SCAN|STATUS|GATT ...>` | Partial | BLE scan and GATT inspect are implemented; status remains reserved |
| `WIFI SCAN` | Implemented | Scan nearby APs |
| `WIFI CONNECT ssid=<ssid> psw=<password>` | Implemented | Start STA connection |
| `WIFI STATUS` | Implemented | Read current STA state |
| `WIFI DISCONNECT` | Implemented | Disconnect from current AP |
| `WIFI DISCOVER` | Implemented | Probe the current IPv4 subnet for reachable hosts |
| `WIFI HTTP preset=<ip|time|location>` | Implemented | Fetch a preset HTTP-backed network lookup |
| `WIFI READ_MDNS host=<hostname>` | Implemented | Resolve an mDNS hostname on the connected network |
| `WIFI PROMISCUOUS <ENTER|EXIT|SURVEY|WATCH|WATCH_STOP>` | Implemented | Enter passive capture mode and run RF observation commands |
| `WIFI BEACON <START\|STOP> [channel=<1-13>] [duration_ms=<ms>]` | Implemented | Broadcast preset fake beacon SSIDs (idle mode only, ≤60 s, low TX power) |
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

### `BLE`

BLE is a top-level command family for BLE discovery and later per-device actions.

Current shape:

- `BLE SCAN` scans nearby BLE advertisers and returns a compact device list
- `BLE STATUS` is reserved for future BLE mode or activity reporting
- `BLE GATT ...` connects to one device, enumerates GATT services/characteristics, and reads readable values

### `BLE SCAN`

Performs a blocking BLE scan and returns nearby advertisers discovered during the scan window.

Request:

```text
BLE SCAN [duration_ms]
```

Successful response format:

```text
BLE_SCAN_START <duration_ms>
BLE_SCAN_COUNT <total>
BLE_DEVICE <mac> RSSI <rssi> COMPANY <company|-> APPEAR <appearance|-> CLASS <class> PROX <Close|Near|Room|Far> ADDR <PUBLIC|RANDOM|PUBLIC_ID|RANDOM_ID|UNKNOWN> CONN <YES|NO> NAME <name|->
BLE_DEVICE <mac> RSSI <rssi> COMPANY <company|-> APPEAR <appearance|-> CLASS <class> PROX <Close|Near|Room|Far> ADDR <PUBLIC|RANDOM|PUBLIC_ID|RANDOM_ID|UNKNOWN> CONN <YES|NO> NAME <name|->
...
BLE_SCAN_DONE
```

Possible extra line when more devices were seen than the result cap:

```text
BLE_SCAN_TRUNCATED <remaining_count>
```

Notes:

- The scan is blocking inside the command handler, similar to `WIFI SCAN`.
- `BLE_SCAN_START` is emitted immediately so the Flipper UI can show timed progress during the blocking scan window.
- The current result cap is 20 devices.
- `duration_ms` is optional. If omitted, the firmware uses its default BLE scan window.
- The Flipper app currently offers `Normal` at 5 seconds and `Deep` at 30 seconds.
- Duplicate advertisements are filtered and devices are deduplicated by address before output.
- `COMPANY` is a compact company label derived from manufacturer data when present; otherwise `-`.
- `APPEAR` is a compact appearance label derived from the standard BLE appearance field when present; otherwise `-`.
- `CLASS` is a user-facing coarse classification inferred from beacon signatures, appearance, manufacturer hints, name fragments, and advertised services.
- `PROX` is a rough proximity bucket derived from RSSI and TX power when available, otherwise from RSSI alone.
- `CONN YES` means the advertisement type is connectable.
- `NAME -` means no device name was present in the advertisement payload.

Possible failures:

```text
ERR BLE_SCAN_BUSY
ERR BLE_SCAN_TIMEOUT
ERR BLE_SCAN_NO_MEM
ERR BLE_SCAN_FAILED
```

Example:

```text
BLE SCAN
BLE_SCAN_COUNT 3
BLE_DEVICE AA:BB:CC:DD:EE:FF RSSI -54 COMPANY Apple APPEAR Watch CLASS Personal PROX Close ADDR RANDOM CONN YES NAME Keyboard K380
BLE_DEVICE 11:22:33:44:55:66 RSSI -68 COMPANY - APPEAR Tag CLASS Tracker PROX Room ADDR PUBLIC CONN NO NAME -
BLE_DEVICE 22:33:44:55:66:77 RSSI -72 COMPANY Google APPEAR Sensor CLASS Sensor PROX Near ADDR RANDOM CONN YES NAME MySensor
BLE_SCAN_DONE
```

### `BLE STATUS`

Reserved for future BLE mode or activity reporting.

```text
BLE STATUS
ERR BLE_STATUS_NOT_IMPLEMENTED
```

### `BLE GATT ...`

Connects to a selected BLE device and performs a one-shot GATT inspect pass.

Request:

```text
BLE GATT mac=<mac> [addr_type=<PUBLIC|RANDOM|PUBLIC_ID|RANDOM_ID>]
```

Successful response format:

```text
BLE_GATT_START <mac>
BLE_GATT_CONNECTED
BLE_GATT_SVC <svc_uuid> NAME <name|->
BLE_GATT_CHR <svc_uuid> <chr_uuid> PROPS <props> NAME <name|->
BLE_GATT_VAL <svc_uuid> <chr_uuid> <decoded_value>
BLE_GATT_RAW <svc_uuid> <chr_uuid> <hex_payload>
...
BLE_GATT_DONE
```

Notes:

- The inspect operation is blocking in the command handler.
- It runs: connect -> discover services -> discover characteristics -> read READ-capable characteristics -> disconnect.
- `PROPS` may include values like `READ`, `WRITE`, `NOTIFY`, `INDICATE`, and `WRITE_NR`.
- Known SIG UUIDs are mapped to compact names (for both services and characteristics); unknown UUIDs use their short hex form.
- `BLE_GATT_VAL` is the decoded/user-facing value, while `BLE_GATT_RAW` carries the original raw bytes as contiguous uppercase hex.
- Characteristic values are compactly decoded when possible (for example battery level) and otherwise emitted as short text or hex.
- If a characteristic read fails, `BLE_GATT_VAL` may contain `read_err <status> <reason>` so permission/auth timing issues are visible instead of silently omitted.
- Common reasons include: `auth_required`, `authorization_required`, `encryption_required`, `read_not_permitted`, `timeout`, and `not_connected`.

Possible failures:

```text
ERR BLE_GATT_INVALID_ARGS
ERR BLE_GATT_BUSY
BLE_GATT_CONNECT_FAILED [status=<code>|stage=timeout]
BLE_GATT_DISCOVER_FAILED [stage=<svc|chr|disconnect|unknown> status=<code>]
```

Usage error example:

```text
BLE GATT
ERR USAGE BLE GATT mac=<MAC> [addr_type=<PUBLIC|RANDOM|PUBLIC_ID|RANDOM_ID>]
```

Unknown BLE subcommands reply with:

```text
ERR UNKNOWN_BLE_COMMAND
```

### `WIFI`

Entering `WIFI` with no subcommand returns usage:

```text
ERR USAGE WIFI <SCAN|STATUS|CONNECT|DISCONNECT|DISCOVER|HTTP|READ_MDNS|PROMISCUOUS>
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
- `psw` is optional for open networks; when omitted it defaults to an empty password.
- `psw=""` is also accepted for open networks.
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
WIFI CONNECT ssid=Guest
WIFI CONNECTING
```

```text
WIFI CONNECT ssid="Office WiFi" psw="correct horse battery staple"
WIFI CONNECTING
```

```text
WIFI STATUS
WIFI STATUS mode=CONNECTED action=NONE state=CONNECTED connected=yes ssid=OfficeWiFi reason=0
```

### `WIFI STATUS`

Returns the current Wi-Fi mode and current action snapshot.

Request:

```text
WIFI STATUS
```

Successful response format:

```text
WIFI STATUS mode=<IDLE|CONNECTED|PROMISCUOUS> action=<NONE|SCANNING|CONNECTING|DISCONNECTING|DISCOVERING|READING_MDNS|REQUESTING_HTTP|ENTERING_PROMISCUOUS|EXITING_PROMISCUOUS|SURVEYING|WATCHING> state=<IDLE|SCANNING|CONNECTING|CONNECTED|PROMISCUOUS> connected=<yes|no> ssid=<ssid|-> reason=<code>
```

### `WIFI HTTP preset=<ip|time|location>`

Performs one of the built-in HTTP lookups while the device is connected to an AP and has an IP address.

Request examples:

```text
WIFI HTTP preset=ip
WIFI HTTP preset=time
WIFI HTTP preset=location
```

Successful response format:

```text
HTTP_LINE <text>
HTTP_LINE <text>
...
HTTP_DONE
```

Usage error:

```text
ERR USAGE WIFI HTTP preset=<ip|time|location>
```

Preset error:

```text
ERR HTTP_UNKNOWN_PRESET
```

Execution failures may return:

```text
ERR HTTP_TIMEOUT
ERR HTTP_NO_MEM
ERR HTTP_FETCH_FAILED
```

Field meanings:

- `mode`: top-level Wi-Fi mode
- `action`: current activity within that mode
- `state`: compatibility summary derived from `mode` and `action`
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
WIFI STATUS mode=IDLE action=NONE state=IDLE connected=no ssid=- reason=0
```

```text
WIFI STATUS
WIFI STATUS mode=CONNECTED action=CONNECTING state=CONNECTING connected=no ssid=OfficeWiFi reason=0
```

```text
WIFI STATUS
WIFI STATUS mode=CONNECTED action=NONE state=CONNECTED connected=yes ssid=OfficeWiFi reason=0
```

### `WIFI PROMISCUOUS <ENTER|EXIT|SURVEY|WATCH|WATCH_STOP>`

Promiscuous-mode commands live under a dedicated `WIFI` subtree.

#### `WIFI PROMISCUOUS ENTER [channel=<n>]`

Enables Wi-Fi promiscuous mode and tunes the radio to the requested channel.

Successful response:

```text
WIFI PROMISCUOUS ENTERED
```

Usage error:

```text
ERR USAGE WIFI PROMISCUOUS ENTER [channel=<n>]
```

Possible failures:

```text
ERR WIFI_MODE_CONFLICT
ERR WIFI_PROMISCUOUS_FAILED
```

Notes:

- `channel` is optional and defaults to `1`.
- Entering promiscuous mode is only allowed from the idle Wi-Fi mode.

#### `WIFI PROMISCUOUS EXIT`

Disables Wi-Fi promiscuous mode and returns the firmware to idle Wi-Fi mode.

Successful response:

```text
WIFI PROMISCUOUS EXITED
```

Possible failures:

```text
ERR WIFI_PROMISCUOUS_FAILED
```

#### `WIFI PROMISCUOUS SURVEY [channels=<comma_list>] [dwell_ms=<ms>] [rssi_min=<dbm>]`

Runs a blocking per-channel survey while promiscuous mode is active.

Successful response format:

```text
SURVEY channel=<n> total=<frames> mgmt=<count> data=<count> ctrl=<count> misc=<count> beacons=<count> deauth=<count> unique=<count> max_rssi=<dbm> duration_ms=<ms>
...
SURVEY_DONE channels=<count> dwell_ms=<ms> duration_ms=<total_ms> recommended=<channel>
```

Possible failures:

```text
ERR WIFI_NOT_PROMISCUOUS
ERR WIFI_PROMISCUOUS_BUSY
ERR WIFI_SURVEY_FAILED
ERR USAGE WIFI PROMISCUOUS SURVEY [channels=<comma_list>] [dwell_ms=<ms>] [rssi_min=<dbm>]
```

Notes:

- `channels` defaults to `1` through `11`.
- `dwell_ms` defaults to `250`.
- `rssi_min` defaults to `-95` and filters weaker frames out of the counts.
- `recommended` is currently the quietest surveyed channel by total observed frames.

Example:

```text
WIFI PROMISCUOUS ENTER channel=1
WIFI PROMISCUOUS ENTERED
WIFI PROMISCUOUS SURVEY channels=1,6,11 dwell_ms=200
SURVEY channel=1 total=142 mgmt=51 data=59 ctrl=28 misc=4 beacons=12 deauth=0 unique=19 max_rssi=-43 duration_ms=200
SURVEY channel=6 total=87 mgmt=34 data=31 ctrl=20 misc=2 beacons=8 deauth=0 unique=11 max_rssi=-48 duration_ms=200
SURVEY channel=11 total=24 mgmt=11 data=8 ctrl=5 misc=0 beacons=3 deauth=0 unique=5 max_rssi=-61 duration_ms=200
SURVEY_DONE channels=3 dwell_ms=200 duration_ms=600 recommended=11
```

#### `WIFI PROMISCUOUS WATCH channel=<n> [interval_ms=<ms>] [rssi_min=<dbm>]`

Starts a continuous fixed-channel observation stream while promiscuous mode is active.

Successful response format:

```text
WATCH_STARTED channel=<n> interval_ms=<ms>
WATCH_LIVE channel=<n> elapsed_ms=<ms> total=<frames> mgmt=<count> data=<count> ctrl=<count> misc=<count> beacons=<count> deauth=<count> unique=<count> max_rssi=<dbm>
WATCH_LIVE channel=<n> elapsed_ms=<ms> total=<frames> mgmt=<count> data=<count> ctrl=<count> misc=<count> beacons=<count> deauth=<count> unique=<count> max_rssi=<dbm>
...
WATCH_SUMMARY channel=<n> duration_ms=<ms> total=<frames> mgmt=<count> data=<count> ctrl=<count> misc=<count> beacons=<count> deauth=<count> unique=<count> max_rssi=<dbm>
WATCH_DEVICE mac=<aa:bb:cc:dd:ee:ff> frames=<count> beacons=<count> max_rssi=<dbm>
WATCH_DEVICE mac=<aa:bb:cc:dd:ee:ff> frames=<count> beacons=<count> max_rssi=<dbm>
...
WATCH_DONE channel=<n> duration_ms=<ms>
```

Possible failures:

```text
ERR WIFI_NOT_PROMISCUOUS
ERR WIFI_PROMISCUOUS_BUSY
ERR WIFI_WATCH_FAILED
ERR USAGE WIFI PROMISCUOUS WATCH channel=<n> [interval_ms=<ms>] [rssi_min=<dbm>]
```

Notes:

- `channel` is required.
- `interval_ms` defaults to `1000` and controls how often `WATCH_LIVE` summaries are emitted.
- `rssi_min` defaults to `-95`.
- The watch continues until `WIFI PROMISCUOUS WATCH_STOP` is received or promiscuous mode is exited.

#### `WIFI PROMISCUOUS WATCH_STOP`

Stops the active continuous watch stream and emits the final summary payload.

Successful response format:

```text
WATCH_SUMMARY channel=<n> duration_ms=<ms> total=<frames> mgmt=<count> data=<count> ctrl=<count> misc=<count> beacons=<count> deauth=<count> unique=<count> max_rssi=<dbm>
WATCH_DEVICE mac=<aa:bb:cc:dd:ee:ff> frames=<count> beacons=<count> max_rssi=<dbm>
...
WATCH_DONE channel=<n> duration_ms=<ms>
```

Example:

```text
WIFI PROMISC WATCH channel=6 interval_ms=1000
WATCH_STARTED channel=6 interval_ms=1000
WATCH_LIVE channel=6 elapsed_ms=1000 total=127 mgmt=41 data=56 ctrl=26 misc=4 beacons=9 deauth=0 unique=14 max_rssi=-47
WATCH_LIVE channel=6 elapsed_ms=2000 total=268 mgmt=83 data=119 ctrl=58 misc=8 beacons=18 deauth=0 unique=26 max_rssi=-44
WIFI PROMISC WATCH_STOP
WATCH_SUMMARY channel=6 duration_ms=2310 total=299 mgmt=94 data=132 ctrl=63 misc=10 beacons=20 deauth=0 unique=31 max_rssi=-44
WATCH_DEVICE mac=AA:BB:CC:DD:EE:FF frames=49 beacons=17 max_rssi=-44
WATCH_DEVICE mac=11:22:33:44:55:66 frames=28 beacons=0 max_rssi=-58
WATCH_DONE channel=6 duration_ms=2310
```

Notes:

- `beacons` is the count of beacon management frames seen from that transmitter during the watch.
- `max_rssi` is the strongest RSSI observed for that transmitter during the watch.
- The app can use beacon-heavy devices as a cautious `Likely AP` heuristic, with frame count as a fallback tie-breaker.

Usage for the subtree root:

```text
ERR USAGE WIFI PROMISCUOUS <ENTER|EXIT|SURVEY|WATCH|WATCH_STOP>
```

### `WIFI BEACON <START|STOP>`

Broadcasts a fixed pack of 8 preset fake beacon SSIDs (e.g. "FBI Surveillance Van", "Pretty Fly for a WiFi") using raw 802.11 frame injection. Requires WiFi to be in idle mode (not connected and not in promiscuous mode). TX power is capped at ~8 dBm and duration is capped at 60 seconds.

The beacons use locally-administered MAC addresses (first octet `0x02`) so they are clearly distinguishable from real hardware.

#### `WIFI BEACON START`

Request:

```text
WIFI BEACON START [channel=<1-13>] [duration_ms=<ms>]
```

Defaults: `channel=6`, `duration_ms=30000`.

Immediate response on success (before task starts):

_(no immediate ACK — `BEACON_STARTED` is emitted by the background task)_

Asynchronous responses:

```text
BEACON_STARTED channel=<n> ssids=<count> duration_ms=<ms>
...beacons are broadcast in background...
BEACON_DONE channel=<n> ssids=<count> duration_ms=<actual_ms>
```

Error responses:

```text
ERR WIFI_MODE_CONFLICT
ERR WIFI_BEACON_BUSY
ERR WIFI_BEACON_FAILED
ERR USAGE WIFI BEACON START [channel=<1-13>] [duration_ms=<ms>]
```

Example:

```text
WIFI BEACON START channel=6 duration_ms=20000
BEACON_STARTED channel=6 ssids=8 duration_ms=20000
...20 seconds later...
BEACON_DONE channel=6 ssids=8 duration_ms=20003
```

#### `WIFI BEACON STOP`

Stops an active beacon session early. The background task emits `BEACON_DONE` once it exits.

Request:

```text
WIFI BEACON STOP
```

Response on success:

_(no immediate ACK — `BEACON_DONE` is emitted by the background task shortly after)_

Error responses:

```text
ERR WIFI_BEACON_NOT_ACTIVE
ERR WIFI_BEACON_FAILED
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
DISCOVER_NETWORK subnet=<network>/<prefix> self=<local_ip> gw=<gateway_ip> hosts=<usable_host_count>
DISCOVER_PROGRESS scanned=<count> total=<usable_host_count> found=<count> current=<last_ip_in_batch>
DISCOVER_FOUND ip=<ip> host=<hostname|-> source=<none|mdns|rdns> mac=<mac|-> vendor=<label|-> role=<GW|WEB|SSH|DNS|NAS|CAM|-> services=<token[+token...]|-> rtt_ms=<round_trip_ms>
DISCOVER_FOUND ip=<ip> host=<hostname|-> source=<none|mdns|rdns> mac=<mac|-> vendor=<label|-> role=<GW|WEB|SSH|DNS|NAS|CAM|-> services=<token[+token...]|-> rtt_ms=<round_trip_ms>
...
DISCOVER_DONE scanned=<count> found=<count> duration_ms=<total_ms>
```

Notes:

- The discovery scan is blocking inside the command handler.
- The current implementation probes each usable host in the connected IPv4 subnet with one ICMP echo request.
- The current implementation probes up to 4 hosts in parallel per batch to stay within typical ESP-IDF socket limits.
- Progress is streamed once per ping batch so a client can show a determinate progress bar while discovery is running.
- Results are streamed only for hosts that respond to the ping.
- The `gw` field reports the current station gateway so clients can badge likely infrastructure devices.
- The firmware attempts to read each responding host from the station ARP cache and emits `mac=` when available.
- `vendor=` is a lightweight built-in OUI hint derived from the MAC prefix when it matches one of the bundled prefixes; otherwise it is `-`.
- `services=` is a compact summary of short TCP probes against a small fixed port set. Tokens currently include `WEB`, `TLS`, `SSH`, `DNS`, `SMB`, and `RTSP`.
- `role=` is an inferred card hint derived from the service probe result. It is emitted as `-` when the probe set does not strongly suggest a role.
- The `source` field is reserved for hostname enrichment. In the current firmware build it is emitted as `none` for all results.
- Service probes run only after a host answers ICMP and use short nonblocking TCP timeouts to keep the scan responsive.
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
DISCOVER_NETWORK subnet=192.168.1.0/24 self=192.168.1.42 gw=192.168.1.1 hosts=253
DISCOVER_PROGRESS scanned=4 total=253 found=1 current=192.168.1.4
DISCOVER_FOUND ip=192.168.1.1 host=- source=none mac=74:83:C2:12:34:56 vendor=- role=DNS services=WEB+TLS+DNS rtt_ms=2
DISCOVER_PROGRESS scanned=8 total=253 found=2 current=192.168.1.8
DISCOVER_FOUND ip=192.168.1.44 host=- source=none mac=24:0A:C4:AA:BB:CC vendor=Espressif role=WEB services=WEB rtt_ms=6
DISCOVER_FOUND ip=192.168.1.87 host=- source=none mac=- vendor=- role=SSH services=SSH rtt_ms=14
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
- The firmware now reports Wi-Fi as a top-level `mode` plus a current `action`.
- Connected-mode commands and promiscuous-mode commands are routed through separate Wi-Fi sub-handlers.
- `WIFI DISCOVER` currently targets the connected IPv4 subnet and is optimized for typical `/24` home LANs.
- This interface is request/response text, not a framed binary protocol.

## Minimal test sequence

```text
READY
PING
PONG
WIFI STATUS
WIFI STATUS mode=IDLE action=NONE state=IDLE connected=no ssid=- reason=0
WIFI SCAN
SCAN_COUNT 2
AP MyAP RSSI -61 AUTH WPA2 CH 6
AP Guest RSSI -80 AUTH OPEN CH 11
SCAN_DONE
WIFI CONNECT ssid=MyAP psw=secret123
WIFI CONNECTING
WIFI STATUS
WIFI STATUS mode=CONNECTED action=NONE state=CONNECTED connected=yes ssid=MyAP reason=0
```