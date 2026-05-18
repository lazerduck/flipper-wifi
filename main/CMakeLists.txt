idf_component_register(
    SRCS
        "main.c"
        "uart.c"
        "parser.c"
        "router.c"
        "led.c"
        "sd_card.c"
        "settings.c"
        "wifi/wifi.c"
        "wifi/promiscuous.c"
        "wifi/connected.c"
        "wifi/beacon.c"
        "ble/ble.c"
        "ble/scan.c"
        "ble/gatt.c"
        "ble/rssi_track.c"
        "zigbee/zigbee.c"
        "zigbee/scan.c"
        "zigbee/listen.c"

    INCLUDE_DIRS
        "."         ## types.h, uart.h, parser.h, router.h, led.h, sd_card.h, settings.h
        "wifi"      ## wifi.h (visible to router.c and wifi sub-modules)
        "ble"       ## ble.h  (visible to router.c and ble sub-modules)
        "zigbee"    ## zigbee.h (visible to router.c and zigbee sub-modules)

    REQUIRES
        driver          ## UART, SPI, GPIO (umbrella for esp_driver_*)
        esp_driver_rmt  ## RMT peripheral used by the led_strip driver
        esp_wifi        ## Wi-Fi stack
        bt              ## BLE / NimBLE stack
        sdmmc           ## SD card protocol driver
        fatfs           ## FAT filesystem for SD card
        led_strip       ## WS2812 driver (managed component: espressif/led_strip)
        nvs_flash       ## NVS — used by Wi-Fi for credential storage
        esp_netif       ## Network interface — required by Wi-Fi
        esp_event       ## Event loop — required by Wi-Fi and BLE
)
