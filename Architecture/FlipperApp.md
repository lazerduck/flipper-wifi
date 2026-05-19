# Flipper App

Companion to the ESP32 firmware, we need an app for flipper to interact with and display the outputs of the ESP.

## Style
The flipper app should make use of the existing flipper tools for lists and button. Where new UI elements are required then we should aim to maintain the flipper aesthetic with a leaning towards the cyber style.

## Start up
During startup we need to ensure the 5v rail is turned on, start up the UART and trigger the ping request to ensure the ESP is there. This needs to include delays for the ESP to initialise, automated retries in the event of an issue. Finally if the start up fails then we should allow the user to retry to start up process with a button press

## Main menu
The main menu should present the user with the following options:
- Wifi
- BLE
- ZigBee
- LED
- SD Card
- Settings

Selecting Wifi, BLE, or ZigBee should swap the ESP in to the appropriate mode

## Wifi
in the Wifi mode the following options should be available
- Connect
- Promiscuous

### Connect
In this setting we should scan for broadcasting APs, and hidden ones if possible, and present cards with the AP name, strength and security type. The user should be able to scroll through the cards in order to select one, or press a button to trigger a rescan.
- Back -> return to the previous menu
- Select (middle button) -> select an AP to join
- Up/Down -> scroll through the various APs
- Lef -> rescan

Once an AP has selected we should then be able to enter a password, or if the AP has no security then we should be able to skip this step. If connection succeeds then we should proceed to the "connected menu":
- Get location details -> trigger a https request to a free API that can tell us our IP, rough location, and time
- Scan local devices -> ping or otherwise query devices on the network to see what device respond. We should check common ports to try to identify what kind of device we have found. These should be presented in cards that can be scrolled through, presenting IP, and estimated type/open ports

### Promiscuous
In this mode the following options should be available
- Channel scan
- Channel listen
- beacon spam

#### Channel scan 
This should listen through the range of channels for 2 seconds each and identify the amount of traffic on each, reporting any recognised APs and the number of packets detected. In the end the user should be able to flip through and see what networks are on what channels and which channel has the least/most traffic

#### Channel listen
In this mode the user should be able to select a channel and then the ESP should listen for and collect any packets being broadcast. It should start scanning and only stop when triggered by the user. When stoppped the user should see a summary of the number of packets, and the number of devices

#### Beacon spam
When triggered should create several fake beacons with obviously fake names

## BLE
The BLE mode should present the following options:
- Scan for devices
- View saved devices

### Scan for devices
Trigger an active BLE scan and display results as scrollable cards. Each card should show:
- Device name (if advertised, otherwise show MAC address)
- MAC address
- RSSI / signal strength
- Advertised services (if any)

The scan runs continuously, updating cards as devices are discovered or their RSSI changes. The user can stop the scan at any time.

- Back -> return to the previous menu
- Up/Down -> scroll through discovered devices
- Select (middle button) -> open the device menu for the selected device
- Right -> stop/restart the scan

#### Device menu (from scan)
When a device is selected from the scan results the user is presented with:
- Save device -> store the device's MAC address and name to the saved devices list
- GATT browser -> connect and browse services/characteristics (see below)
- Strength graph -> begin a long-running RSSI graph (see below)

### View saved devices
Display a scrollable list of previously saved devices showing their name and MAC address.

- Back -> return to the previous menu
- Up/Down -> scroll through saved devices
- Select (middle button) -> open the saved device menu

#### Saved device menu
When a saved device is selected, first attempt to detect whether it is still in range by performing a short passive scan for its MAC address. Display the result (in range / out of range) before presenting the options:
- GATT browser -> connect and enumerate all GATT services and characteristics, displaying them in a scrollable tree (service → characteristics, read-only)
- Strength graph -> begin a long-running RSSI graph (see below)
- Forget device -> remove the device from the saved list (with a confirmation prompt)

### GATT browser
Connect to the target device and read all available GATT services and their characteristics. Present results as a scrollable tree: each service can be expanded to show its characteristics, their UUIDs, properties (read/notify/indicate/etc.), and current value where readable. The user can scroll through and select a characteristic to see its full value on a detail screen.

- Back -> disconnect and return to the device menu

### Strength graph
Begin a continuous RSSI poll for the target device, plotting signal strength over time as a scrolling line graph. This mode is intended to help physically locate a device by moving towards it and watching the graph climb. The graph should update in real time and display the current RSSI value numerically alongside the graph.

- Back -> stop polling and return to the device menu

## LED
A simple playground for controlling the onboard RGB LED. The full 0–255 range per channel is reduced to a smaller number of steps (e.g. 16 steps of 16) to keep adjustment quick and usable on the Flipper's buttons.

The screen displays the three colour components (R, G, B) as both a numeric value and a simple bar indicator. One component is highlighted as selected at any time.

- Up/Down -> increase or decrease the selected component's value by one step
- Left/Right -> cycle the selection between R, G, and B
- Select (middle button) -> toggle the LED on/off (values are retained while off so they can be restored on)
- Back -> return to the main menu

## SD Card
The SD Card menu presents the following options:
- Details
- Explorer
- Format

### Details
Display a summary of the mounted SD card:
- Presence / mount status
- Total capacity
- Used space and free space
- Filesystem type (FAT32 etc.)

If no card is detected, show an appropriate message. A "Remount" option should be available to retry detection without leaving the screen.

- Back -> return to the SD Card menu

### Explorer
A simple file and directory browser starting at the root of the SD card.

Each entry shows its name and, for files, its size. Directories are visually distinguished (e.g. with a leading `/` or folder icon if the font supports it).

- Up/Down -> scroll through entries in the current directory
- Right / Select (middle button) -> enter a directory or open the file action menu
- Left / Back -> go up one directory level (or back to SD Card menu from root)

#### File action menu
When a file is selected the user is presented with:
- View contents -> display the file as scrollable text, capped at a configurable limit (e.g. first 4 KB) with a notice if the file was truncated
- Delete -> remove the file with a confirmation prompt

#### Directory action menu
When a directory is selected the user can also:
- Delete -> remove the directory and all its contents with a confirmation prompt (show item count before confirming)

### Format
Re-initialise the SD card, wiping all data and creating a fresh filesystem. Because this is destructive, require a two-step confirmation: first warn the user that all data will be lost, then require a second explicit confirmation before proceeding. Display progress while formatting and confirm success or failure on completion.

- Back (at any confirmation step) -> cancel and return to the SD Card menu

## Settings
Settings are stored on the SD card. If no card is present the settings screen should still be accessible but changes cannot be saved — display a warning in that case and revert to defaults on next boot.

### Logging
Controls how much data the firmware writes to the SD card during operation. Options (TBD — exact levels to be defined):
- Off — no logging
- Basic — key events only (mode changes, connection results, errors)
- Verbose — full capture data for radio modes (WiFi packets, BLE scan results, etc.)

> **Stub** — additional settings categories to be added as requirements become clearer.
