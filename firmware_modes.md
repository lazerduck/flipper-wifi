# Wi-Fi Feature Map

This document lays out the Wi-Fi feature structure for the ESP32 firmware, separating normal station-mode operations from promiscuous-mode operations.

The intent is to make the high-level behaviour, dependencies, and likely firmware states clear before expanding the command set further.

---

## Top-level structure

```text
WiFi
|
|- Scan
|
|- Connect (join an AP)
|  |
|  |- Discover (scan for IPs on local network)
|  |
|  |- Send pre-configured requests
|  |
|  |- mDNS / local service discovery
|  |
|  `- Other connected-mode features (to be defined)
|
`- Enter Promiscuous Mode
   |
   |- Survey
   |
   `- Watch
```

---

## Overview

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

These can be added later without changing the top-level model.
