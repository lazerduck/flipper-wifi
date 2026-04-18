idf_component_register(
    SRCS
        "main.c"
        "command/command_router.c"
        "modules/status_led/status_led.c"
        "modules/ping/ping_command.c"
        "modules/wifi/wifi_command.c"
        "modules/wifi/wifi_discovery.c"
        "modules/wifi/wifi_mdns.c"
        "modules/wifi/wifi_manager.c"
        "modules/send/send_command.c"
        "modules/query/query_command.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_rmt esp_driver_uart esp_event esp_netif esp_timer esp_wifi lwip nvs_flash)