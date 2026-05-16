# Fuse-Radio

This is a piece of firmware running on a custom ESP32 c6 board with an RGB LED, USB, and SD card. This firmware will be paired with an app on the flipper zero, connecting over UART

The project is intended to extend the flipper zero in to Wifi, BLE, and ZigBee. Allowing diagnostic and investigatory tools

## Pins

### UART
Default to UART port 1 as UART0 is usually shared with console and flash
The default BAUD rate is 115200
TX pin is 1
RX pin is 0

This will be linked up with the flippers RX and TX appropriately

### USB

### SD card