# Fuse-Radio

This is a piece of firmware running on a custom ESP32 c6 board with an RGB LED, USB, and SD card. This firmware will be paired with an app on the flipper zero, connecting over UART

The project is intended to extend the flipper zero in to Wifi, BLE, and ZigBee. Allowing diagnostic and investigatory tools.

The flipper will run the UI and trigger commands over UART, the ESP32 will perform the action and then processed data will be returned to the flipper

## Pins

### UART
Default to UART port 1 as UART0 is usually shared with console and flash
The default BAUD rate is 115200
TX pin is 1
RX pin is 0

This will be linked up with the flippers RX and TX appropriately

### SD card and LED

SD_CARD_PIN_CS 4
SD_CARD_PIN_SCK 5
SD_CARD_PIN_MISO 6
SD_CARD_PIN_MOSI 7
SD_CARD_PIN_CD 2

led pin - 8

The LED is a WS2812, essentially a neopixel

## Feature set

Here is an overview of the feature set

### Wifi

#### Promiscuous mode

In promiscuous mode the device does not connect to a network but instead listens
- Scan the usage on each channel in order to see which are the busiest
- Scan a specific channel to detect unique devices and make assumptions about the devices if possible (e.g. likely access point)
- Generate Beacon spam to enable testing of wifi UI and as a bit of a party trick

#### Connected mode

Here we actually connect and do things on the network
- Scan for SSIDs and connect to one successfully (public or private) to enter the mode properly
- Perform an IP address sweep, checking if port 22 is open, and checking other common indicators that might help identify the device
- Call some free internet services such as to find the current location, time, IP and local weather

### BLE

- Scan for devices. We see what devices we can find and then present them, denoting strength(distance), and any other identifiable information
- GATT inspect. Attempt to connect and resolve GATT requests so that we can tell what device we are connecting to and what it can do
    - Primarily we want to be able to read the data back and try to parse it to something readable
- Graph the distance. We want to be able to select a device and listen to its broadcasts, logging the signal strength to allow us to navigate to the device
- Save the device. Record information about the device so we can recognise it if we find it later

### ZigBee

This is mostly unexplored territory but here are some potential ideas
- Scan for traffic on a range of channels
- Listen to traffic on a specific channel
- Emulate a button(s) (ZHA/home assistant integration). Essentially act as a custom remote

### LED

The LED should change to reflect the mode and current action. E.g. Turn blue in wifi mode, blink when scanning etc

### SD card

The firmware should be able to use the SD card for storing of logs or other data that could be useful either for deeper inspection or for portability
- Format/wipe, depending if already formatted
- Explore. See what files are on the SD card

### Settings

There are some things we may or may not want the firmware to do, we can record that here
- Requires the SD card to be modified as that is where the config will be stored, otherwise we show default and assume defaults
- Logging
    - Allows detailed logging of the various radio capture, allowing us to decide if logging is on or off, and to what degree

### Ping/utility

- The firmware should expose a simple ping/pong listener so the flipper app can check the device is working
- The time should be passed from the flipper (if the esp can keep time)

## UART protocol

In the UART we will send commands and arguments.
Commands will be made modular. E.g. WIFI_PROMISCUOUS_CHANNELSCAN time="30"
This will be used by the firmware to send the command to the correct module and to validate the mode
Commands need to be able to handle sending and receiving text with spaces, numbers, and arrays

## Firmware layout

First, the firmware will have 3 modules it always loads on start
- SD card (Check if present and then mount if possible)
- Settings
- LED

After that the user enters the main menu where they can interact with the SD card to explore it, configure the settings, and play with the LED

At the minute our mode is undefined until the user chooses between the 3 radios. Once chosen, we init that radio and we and in that mode. Upon attempting to leave that mode (via back in the flipper app) we will trigger a restart on the ESP32 to ensure it is in a position to initialise another radio. The restart completes in under a second and the Flipper app simply waits for the next `PONG` response, making it transparent to the user


