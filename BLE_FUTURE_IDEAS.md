# BLE Future Ideas

This is a lightweight holding document for BLE ideas that fit the current direction of the project but are not implemented yet.

The focus should stay on:

- useful discovery
- user-owned device workflows
- understandable signal/presence tooling
- read or notify style inspection before anything more invasive

---

## High-value next ideas

### 1. Streaming distance estimator

Purpose:

- give the user a live sense of whether a saved device is getting closer or farther
- make saved-device tracking more useful than a one-shot RSSI refresh

Good shape for this project:

- repeated short BLE scans aimed at one selected or saved device
- smoothed RSSI instead of raw jumpy values
- user-facing buckets such as `Very close`, `Close`, `Near`, `Far`, `Weak`
- optional trend text such as `approaching`, `steady`, `moving away`

Notes:

- this should be presented as an estimate, not true distance
- RSSI is noisy, so smoothing and confidence matter more than fake precision
- a simple trend meter is probably better UX than showing exact meters

### 2. GATT inspect

Purpose:

- let the user inspect what services and characteristics a selected device exposes
- make saved devices more actionable and interesting

Good first scope:

- connect to a selected or saved device
- enumerate primary services
- enumerate characteristics per service
- show characteristic properties such as `read`, `write`, `notify`, `indicate`

Good UI shape:

- device -> services list -> characteristics list -> characteristic actions

### 3. GATT read and notify listen

Purpose:

- move from static inspection into useful live data for owned devices

Good first scope:

- read readable characteristics
- subscribe to notify-capable characteristics
- stream notifications into a simple live result view
- allow stop/unsubscribe cleanly

Examples of good targets:

- sensors
- tags
- custom dev boards
- battery/service telemetry

---

## Related ideas in the same category

### Saved-device live presence watch

Purpose:

- keep checking whether a saved device is still around without requiring full rescans each time

Possible outputs:

- last seen age
- smoothed signal strength
- presence state such as `here`, `fading`, `gone`

### Advertisement history and diff

Purpose:

- make repeat scans more meaningful by showing what changed

Possible outputs:

- new devices since last scan
- devices no longer seen
- name/company/class changes
- strongest recent RSSI per device

### Manufacturer data and service-data decoder view

Purpose:

- expose more meaning from advertisements without dumping raw hex by default

Good first scope:

- beacon-family detection
- tracker-family hints
- known manufacturer payload summaries
- service-data summaries for common formats

This should stay compact and user-facing first, with raw details as a secondary drill-down.

### Service fingerprinting

Purpose:

- classify unknown devices better once GATT inspection exists

Possible outputs:

- probable sensor
- probable HID
- probable audio accessory
- probable health device
- probable custom/dev board

This is useful both for one-off scans and for saved-device summaries.

### Characteristic favorites

Purpose:

- let the user bookmark a useful characteristic on a saved device

Examples:

- battery level
- sensor value
- firmware version
- live notify stream

This would reduce the amount of menu diving needed for repeat use.

### Device capability summary

Purpose:

- provide a compact "what can I do with this device" page after scan or GATT inspect

Example sections:

- advertisement summary
- connectability
- notable services
- readable values
- notify streams available

### Calibration for the distance estimator

Purpose:

- make the streaming proximity view less noisy for a user-owned device

Possible options:

- fast smoothing
- stable smoothing
- optional calibration against a known close range

This should be framed as signal calibration, not true ranging.

### Owned-device safe write actions

Purpose:

- eventually allow limited write interactions for devices the user owns and understands

Examples of acceptable direction:

- toggling a known dev-board feature
- writing configuration to a custom sensor
- interacting with a user-built BLE peripheral

This should stay explicitly opt-in and secondary to read/notify workflows.

---

## Sensible implementation order

1. Streaming distance estimator for saved devices
2. GATT inspect
3. GATT read
4. GATT notify listen
5. Saved characteristic favorites
6. Advertisement history/diff
7. Service fingerprinting improvements

---

## Product notes

- Prefer saved-device flows over generic interaction with arbitrary nearby devices.
- Prefer read and notify before write.
- Prefer compact summaries in the Flipper UI over raw BLE internals.
- When deeper details are useful, put them behind one more menu step.
- For distance-like features, use confidence buckets and trends rather than fake exact meters.