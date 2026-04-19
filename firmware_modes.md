# Radio Feature Map

This document lays out the current Wi-Fi feature structure and the planned BLE feature structure for the ESP32 firmware.

The intent is to make the high-level behaviour, dependencies, and likely firmware states clear before expanding the command set further.

---

## Top-level structure

```text
Radio
|
|- WiFi
|  |
|  |- Scan
|  |
|  |- Connect (join an AP)
|  |  |
|  |  |- Discover (scan for IPs on local network)
|  |  |
|  |  |- Send pre-configured requests
|  |  |
|  |  |- mDNS / local service discovery
|  |  |
|  |  `- Other connected-mode features (to be defined)
|  |
|  `- Enter Promiscuous Mode
|     |
|     |- Survey
|     |
|     `- Watch
|
`- BLE
   |
   |- Scan
   |  |
   |  |- BLE zoo / nearby classification
   |  |
   |  `- Select a scanned device
   |     |
   |     |- Save as owned / tracked device
   |     |
   |     `- Inspect / listen for GATT
   |
   `- Saved Devices
      |
      |- Refresh / check signal strength
      |
      `- Inspect / listen for GATT
```

---

## Overview

The radio functionality is currently split into a mature Wi-Fi branch and a planned BLE branch.

The Wi-Fi functionality currently falls into three broad areas:

### 1. Scan

This is standard Wi-Fi network discovery.

The ESP32 scans for nearby access points and reports metadata such as:

* SSID
* BSSID
* channel
* RSSI
* security type

This is the basic discovery step and is useful both on its own and as input for other actions.

### 2. Connect

This joins a normal Wi-Fi network as a station.

Once connected to an AP, the ESP32 can perform ordinary IP-based network actions. This is where higher-level features live, such as discovering devices on the LAN or sending HTTP requests.

### 3. Enter Promiscuous Mode

This puts the Wi-Fi radio into a passive listening mode.

In this mode the ESP32 is no longer operating as a normal network client in the usual sense. Instead, it listens to 802.11 traffic on one channel at a time and reports what it sees.

This is used for radio analysis features such as Survey and Watch.

### 4. BLE

BLE should start from a simpler discovery-first model.

The first BLE capability should be scanning nearby advertisements and turning that into something interesting and understandable on the Flipper.

That means the initial BLE path should be:

1. scan nearby BLE devices
2. classify what is nearby in a lightweight way
3. let the user select one scanned device
4. allow the user to save that device for later
5. allow later inspection or listening for GATT on devices that make sense to inspect

This keeps BLE focused on discovery, ownership, and repeatable interaction rather than trying to jump straight into arbitrary connect-and-control behaviour.

---

## Planned BLE branch

The intended BLE structure is:

```text
BLE
|
|- Scan
|  |
|  |- BLE zoo
|  |
|  `- Device details / actions
|     |
|     |- Save device
|     |
|     `- Listen for GATT
|
`- Saved Devices
   |
   |- Check strength / presence
   |
   `- Listen for GATT
```

This is intentionally tiered.

The user starts with broad discovery, then narrows into one device, then optionally promotes that device into a saved list for repeat use.

### BLE Scan

BLE scan is the entry point.

Purpose:

* find nearby BLE advertisers
* show enough metadata to make the results interesting
* provide the source list for later save or inspect actions

Expected output should include compact fields such as:

* name if advertised
* MAC address
* RSSI
* connectable flag if known
* manufacturer or service hints if known
* a coarse category if one can be inferred

This is the BLE equivalent of Wi-Fi scan, but with more emphasis on interpretation because raw BLE advertisements are otherwise not very meaningful to a user.

### BLE zoo

BLE zoo is a presentation layer on top of scan results.

Purpose:

* make scanning feel more alive and informative
* bucket nearby devices into understandable groups
* let the user quickly explore what kinds of devices are around

Examples of useful coarse groups:

* beacon
* sensor
* wearable
* audio
* HID / input device
* dev board
* unknown

The important point is that BLE zoo is not a separate radio primitive.

It is a better way to present scan results.

### Selected scanned device

After a scan, the user should be able to select one item and enter a submenu for that device.

Initial actions should be simple:

* save device
* inspect or listen for GATT

This is the point where the flow changes from broad discovery to one-device interaction.

### Save device

Saving a scanned BLE device means the user is marking it as relevant for later repeat actions.

Expected purpose:

* avoid re-finding the same device manually every time
* support repeat strength checks
* support later GATT-oriented inspection workflows
* distinguish user-owned or intentionally tracked devices from one-off nearby scan noise

This is especially useful for devices the user owns, such as tags, sensors, earbuds, keyboards, badges, or custom boards.

### Saved Devices

Saved Devices should exist as a root-level BLE branch, not only as an action attached to a recent scan.

Purpose:

* provide a stable list of user-important devices
* allow quick presence or strength checks
* allow direct entry into later per-device tools

This avoids making scan history the only way to reach a known device.

### Check strength / presence

For a saved BLE device, one useful action is checking whether it is nearby and how strong its signal is.

Expected behaviour:

* perform repeated short refreshes
* attempt to find the saved device again
* report whether it is seen
* show current or recent RSSI

This becomes a lightweight presence meter for the user’s own devices.

### Listen for GATT

The phrase “listen for GATT” should be interpreted carefully.

GATT is generally a connection-oriented interaction model rather than passive observation in the same sense as scan.

For this project, the intended meaning should be:

* inspect exposed services and characteristics on a selected or saved device when appropriate
* optionally subscribe to notifications on devices the user owns or intentionally interacts with
* avoid treating arbitrary nearby devices as generic targets for unexplained writes or intrusive probing

So the likely BLE interaction ladder is:

1. scan
2. select device
3. save if useful
4. inspect services / characteristics
5. optionally subscribe to readable or notify-capable characteristics on supported devices

That is a sensible foundation and avoids trying to make the first BLE pass do too much.

---

## Notes on operating modes

Normal connected mode and promiscuous mode should be treated as separate firmware states.

That means the firmware model is approximately:

```text
Idle
 -> Scan
 -> Connect
 -> Connected Operations
 -> Disconnect
 -> Promiscuous Mode
 -> Survey / Watch
 -> Exit Promiscuous Mode
 -> Idle
```

In practice:

* **Connect** is for network participation
* **Promiscuous Mode** is for passive observation
* channel hopping is suitable for Survey
* fixed-channel observation is suitable for Watch

Promiscuous-mode features should generally not be expected to run at the same time as connected IP features.

---

## Existing features

The following already exist:

* Scan
* Connect
* Discover

These form the basis of the current Wi-Fi capability.

---

## Connected-mode branch

### Connect (join an AP)

This action associates the ESP32 with an access point and obtains network connectivity.

Expected responsibilities:

* join the selected AP
* report connection success or failure
* report assigned IP address
* provide status information
* allow later disconnect

This branch is the basis for all IP-level operations.

### Discover

This performs local network discovery after the ESP32 is connected.

Expected meaning:

* inspect the local subnet
* identify responsive IPs or hosts
* possibly use ARP, ping-style probing, or similar techniques depending on implementation

Expected output might include:

* IP address
* MAC address if available
* host name if available
* response type or protocol hint

This is not Wi-Fi radio discovery; it is LAN discovery after joining the network.

### Send pre-configured requests

This branch is for useful actions that require being connected to the network.

Examples:

* send HTTP GET requests
* call known API endpoints
* fetch device status pages
* query simple web interfaces

This keeps the firmware practical by allowing repeatable network actions without turning the ESP32 into a full browser.

### mDNS / local service discovery

This is a likely future connected-mode feature.

Expected purpose:

* discover devices advertising services on the LAN
* detect hostnames such as `printer.local` or `nas.local`
* identify common services such as HTTP, AirPlay, printers, or media devices

This can complement Discover by finding service-aware devices, not just responsive IPs.

---

## Promiscuous-mode branch

### Enter Promiscuous Mode

This switches the radio from normal station behaviour into passive frame capture.

Expected behaviour:

* tune to one Wi-Fi channel at a time
* receive nearby 802.11 frames regardless of destination
* classify and count frame types
* inspect metadata such as RSSI, MACs, channel, and subtype

This branch should be thought of as RF observation rather than network participation.

---

## Survey

### Purpose

Survey is intended to answer:

**How busy are the channels in this area?**

This is an environment-level measurement rather than a network-level measurement.

### Expected behaviour

Survey should:

* step through one or more channels
* listen on each channel for a defined dwell period
* count or weight observed traffic
* return a summary per channel

Typical process:

1. choose a channel list
2. tune to the first channel
3. listen for a short period
4. record frame statistics
5. move to the next channel
6. collate results

### Expected inputs

Possible parameters:

* channel list
* dwell time per channel
* total time
* RSSI filter threshold

Examples:

* survey all 2.4 GHz channels
* survey channels 1, 6, and 11 only
* survey only channels already seen during Scan

### Expected outputs

Survey results should ideally include per-channel information such as:

* channel number
* total frame count
* beacon count
* data frame count
* strongest signal seen
* number of unique transmitters seen
* derived congestion score

Example conceptual output:

```text
Ch 1  : heavy   (142 frames, 5 APs)
Ch 6  : medium  (67 frames, 3 APs)
Ch 11 : light   (21 frames, 1 AP)
Recommended: 11
```

### Practical meaning

Survey is useful for:

* finding crowded channels
* comparing RF conditions between areas
* recommending a better AP channel
* giving a quick sense of whether 2.4 GHz is saturated

### Important limitation

Survey is not a true throughput meter.

It is measuring observed traffic activity, not exact network bandwidth usage.

---

## Watch

### Purpose

Watch is intended to answer:

**What is happening on this particular channel or network over time?**

This is a focused observation mode rather than a broad sweep.

### Expected behaviour

Watch should:

* remain on a fixed channel
* optionally target a selected AP / BSSID / SSID
* continuously observe nearby 802.11 traffic
* provide rolling or periodic updates

Unlike Survey, Watch does not primarily hop channels.

### Expected inputs

Possible parameters:

* channel
* BSSID
* SSID
* duration
* update interval
* RSSI threshold

### Expected outputs

Watch results might include:

* selected channel
* AP visibility / presence
* RSSI of the AP
* beacon rate
* data frame rate
* management frame events
* estimated number of unique clients seen
* disconnect / deauth events
* overall activity level

Example conceptual output:

```text
Watching: HomeWifi
Channel : 6
RSSI    : -48 dBm
Clients : 7 seen
Traffic : high
Deauths : 0
Status  : stable
```

### Practical meaning

Watch is useful for:

* checking whether a specific AP is active
* seeing whether a network appears busy or idle
* observing client presence over time
* detecting disconnect bursts or instability
* checking whether a selected AP is heavily used

### Important limitation

Watch can infer activity and presence, but it will usually not reveal useful payload content because most real Wi-Fi traffic is encrypted.

Its value is mainly in frame metadata, event observation, and activity patterns.

---

## Difference between Survey and Watch

```text
Survey = broad channel sweep
Watch  = focused fixed-channel observation
```

### Survey

* wider scope
* shorter dwell per channel
* compares channels
* good for congestion overview

### Watch

* narrower scope
* longer observation time
* focuses on one channel or network
* good for behaviour and stability monitoring

---

## Suggested dependency view

```text
WiFi
|
|- Scan
|  `- identifies APs, channels, RSSI, security
|
|- Connect
|  |
|  |- requires AP selection / credentials
|  |
|  |- Discover
|  |- Send pre-configured requests
|  |- mDNS / local service discovery
|  `- other IP-level actions
|
`- Enter Promiscuous Mode
   |
   |- requires radio-only passive mode
   |
   |- Survey
   |  `- depends on channel stepping + frame counting
   |
   `- Watch
      `- depends on fixed-channel observation + event tracking
```

---

## Recommended firmware interpretation

At firmware level, it likely makes sense to treat this as a stateful command model.

Example conceptual states:

* Idle
* Scanning
* Connected
* Discovering
* SendingRequest
* Promiscuous
* Surveying
* Watching

That keeps it clear which commands are valid in which state.

Example implications:

* Discover requires Connected
* Send pre-configured requests requires Connected
* Survey requires Promiscuous
* Watch requires Promiscuous
* Connect and Survey should not run simultaneously

---

## Immediate design takeaway

The current structure is already sensible:

* **Scan** for AP discovery
* **Connect** for LAN-level operations
* **Promiscuous Mode** for RF analysis

Under promiscuous mode, the strongest initial features are:

* **Survey** for channel congestion
* **Watch** for fixed-channel network observation

These two features are distinct, useful, and easy to explain to a user.

---

## Future expansion ideas

Connected mode:

* DNS lookup
* port checks for common services
* device fingerprint hints
* simple HTTP response summarisation

Promiscuous mode:

* deauth watcher
* hidden SSID detection
* nearby device count
* rolling channel history
* best-channel recommendation

BLE:

* BLE zoo categories and counts
* saved-device presence tracking
* basic service and characteristic browser
* notification listener for supported owned devices
* coarse vendor and device-type hints

These can be added later without changing the top-level model.
