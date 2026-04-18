# Feature Tracker

This document tracks the broad strokes of what the Flipper app plus the ESP32 firmware can do today.

It is intentionally higher level than the UART protocol reference. Use this file to track product-level capabilities and user-facing scope. Use `UART_COMMANDS.md` for the wire format and exact command/response details.

---

## System shape

The project is split into two cooperating pieces:

- Flipper app: user interface, command launcher, progress views, and result views
- ESP32 firmware: Wi-Fi radio control, network operations, and UART command server

The Flipper app is the controller. The ESP32 does the actual Wi-Fi and IP work.

---

## Current capability summary

| Area | Status | What it does |
| --- | --- | --- |
| Device link | Implemented | Detects the ESP32 board over UART and verifies liveness with `READY` and `PING`/`PONG` |
| Wi-Fi scan | Implemented | Scans nearby access points and returns SSID, RSSI, auth mode, and channel |
| Wi-Fi connect | Implemented | Starts a station-mode connection to a selected AP |
| Wi-Fi status | Implemented | Reports current mode, action, connection state, SSID, and disconnect reason |
| Wi-Fi disconnect | Implemented | Disconnects from the current AP |
| LAN discovery | Implemented | Probes the active IPv4 subnet and streams progress plus responsive hosts |
| HTTP presets | Implemented | Fetches a small set of built-in HTTP-backed lookups: public IP, time, and approximate location |
| mDNS lookup | Implemented | Resolves `.local` hostnames on the connected network |
| Promiscuous enter/exit | Implemented | Switches the radio into and out of passive packet capture mode |
| RF survey | Implemented | Surveys one or more channels and reports traffic volume and channel recommendation |
| RF watch | Implemented | Watches one channel continuously and streams live plus final packet statistics |
| Device ranking in watch results | Implemented | Surfaces top observed transmitters with frame counts, beacon counts, and strongest RSSI |
| SEND command | Stub | Reserved, not implemented |
| QUERY command | Stub | Reserved, not implemented |

---

## User-facing flows

### 1. Bring-up and board detection

The app can:

- detect that the ESP32 firmware is alive
- verify the command path with `PING`
- show board-ready vs error states

### 2. Normal Wi-Fi workflow

The connected-mode workflow is:

1. scan for nearby APs
2. choose an SSID
3. submit credentials
4. monitor connection status
5. run connected-mode tools
6. disconnect when done

Connected-mode tools currently include:

- subnet discovery
- preset HTTP lookups
- single-host mDNS resolution

### 3. Passive RF workflow

The promiscuous-mode workflow is:

1. enter passive capture mode
2. run either a survey or a live watch
3. inspect per-channel or per-device results
4. stop the watch or exit promiscuous mode

This branch is for RF observation, not IP networking.

---

## App responsibilities

The Flipper app currently provides:

- menu-driven entry into Wi-Fi and promiscuous tools
- scan list presentation
- connection flows for SSID and password entry
- status and error presentation
- progress screens for discovery and survey operations
- result screens for discovery, HTTP, mDNS, survey, and watch outputs
- live watch presentation while capture is running
- repeat actions for selected survey presets and last watch channel

The app is not doing the network work itself. It is a thin controller and viewer on top of the ESP32 UART interface.

---

## ESP firmware responsibilities

The ESP32 firmware currently provides:

- UART command parsing and response streaming
- Wi-Fi station scanning
- AP connection and disconnect control
- current Wi-Fi mode and action tracking
- subnet probing over the active STA link
- simple HTTP client requests for built-in lookups
- mDNS hostname resolution
- promiscuous-mode radio control
- per-channel survey capture
- fixed-channel live watch capture
- summarised transmitter ranking for watch results

---

## Operating modes

The firmware effectively has three top-level Wi-Fi modes:

- `IDLE`: not connected and not in promiscuous capture
- `CONNECTED`: station mode with optional IP-based operations
- `PROMISCUOUS`: passive frame capture mode

These modes matter because the feature set is split along them:

- connected-mode features need an AP connection and an IP address
- promiscuous features need the radio in passive capture mode
- connected IP operations and passive capture should be treated as separate branches of behavior

---

## Connected-mode feature set

### Scan nearby APs

Purpose:

- discover nearby networks
- inspect channel usage at a basic network-list level
- feed the connect flow

Outputs include:

- SSID
- RSSI
- auth mode
- primary channel

### Connect and status

Purpose:

- join an AP as a station
- track whether the board is connecting, connected, or idle
- expose the selected SSID and last disconnect reason

### Local subnet discovery

Purpose:

- probe the connected IPv4 subnet
- stream discovery progress
- list responsive hosts

Current shape:

- ICMP-based probing
- progress updates during the run
- responsive hosts reported with IP and round-trip time
- hostname source field reserved for future enrichment

### HTTP preset lookups

Purpose:

- provide a few useful network-backed lookups without exposing a general HTTP client UI

Current presets:

- public IP
- current UTC time
- approximate geolocation from WAN IP

### mDNS lookup

Purpose:

- resolve a local hostname such as `printer.local`
- help identify useful devices on the current LAN

Current shape:

- single-host query by name
- IPv4 or IPv6 response reporting when available
- truncated-result reporting when more answers exist than the output limit

---

## Promiscuous-mode feature set

### Enter and exit passive capture mode

Purpose:

- switch the radio from normal station behavior into packet observation
- provide a stable base for survey and watch operations

### Survey

Purpose:

- compare channel activity across one or more channels
- help choose a quieter channel

Current outputs include:

- total frames
- management, data, control, and misc counts
- beacon and deauth counts
- unique transmitter count
- strongest observed RSSI
- recommended channel

### Watch

Purpose:

- observe one channel continuously
- stream live counts while capture is active
- produce a final summary when stopped

Current outputs include:

- live aggregate traffic summary
- final aggregate traffic summary
- per-device summary rows for top observed transmitters
- frame count, beacon count, and strongest RSSI per device

This is currently the most inspection-oriented RF feature in the project.

---

## Integration model

The current integration model is request/response over UART.

That means:

- the Flipper app sends plain-text commands
- the ESP32 streams plain-text result lines back
- long-running actions report progress by sending multiple lines
- the app reconstructs higher-level UI state from those lines

This keeps the ESP32 firmware small and focused while letting the Flipper app own the UX.

---

## Notable limitations

Current limitations that matter at the feature level:

- `SEND` and `QUERY` are placeholders only
- HTTP is limited to built-in presets rather than arbitrary URLs
- mDNS is single-host lookup rather than full service browsing
- discovery is aimed at typical small IPv4 LANs rather than large networks
- promiscuous capture is observation-oriented and does not decode higher-level protocols in depth
- connected-mode tools and promiscuous-mode tools should be treated as separate operating branches

---

## Good future sections to extend

If this file becomes the main capability tracker, useful next categories would be:

- BLE features
- richer LAN discovery and labeling
- service browsing beyond single mDNS host lookup
- saved targets or repeatable workflows
- packet classification or protocol-specific RF insights
- custom query/send semantics once those commands are real