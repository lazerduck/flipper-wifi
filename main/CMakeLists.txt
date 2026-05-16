set(ZIGBEE_COMPAT_PATCH_SCRIPT "${CMAKE_SOURCE_DIR}/tools/zigbee_compat_patch.cmake")
if(EXISTS "${ZIGBEE_COMPAT_PATCH_SCRIPT}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DPROJECT_DIR=${CMAKE_SOURCE_DIR} -P ${ZIGBEE_COMPAT_PATCH_SCRIPT}
        RESULT_VARIABLE zigbee_compat_patch_result
    )
    if(NOT zigbee_compat_patch_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply Zigbee compatibility patch")
    endif()
endif()

idf_component_register(
    SRCS
        "main.c"
        "command/command_router.c"
        "modules/ble/ble_command.c"
        "modules/ble/ble_gatt.c"
        "modules/ble/ble_manager.c"
        "modules/status_led/status_led.c"
        "modules/status_led/status_led_command.c"
        "modules/ping/ping_command.c"
        "modules/system/system_command.c"
        "modules/system/system_mode_guard.c"
        "modules/system/system_mode_manager.c"
        "modules/wifi/connected/connected_command.c"
        "modules/wifi/connected/wifi_discovery.c"
        "modules/wifi/connected/wifi_http.c"
        "modules/wifi/connected/wifi_mdns.c"
        "modules/wifi/promiscuous/promiscuous_command.c"
        "modules/wifi/promiscuous/wifi_promiscuous.c"
        "modules/wifi/beacon/beacon_command.c"
        "modules/wifi/beacon/wifi_beacon.c"
        "modules/wifi/wifi_command.c"
        "modules/wifi/wifi_manager.c"
        "modules/sd/sd_card_manager.c"
        "modules/sd/sd_command.c"
        "modules/zigbee/zigbee_command.c"
        "modules/zigbee/zigbee_manager.c"
        "modules/zigbee/zigbee_profile_store.c"
        "modules/zigbee/zigbee_stack.c"
        "modules/settings/settings_manager.c"
        "modules/settings/settings_command.c"
        "modules/send/send_command.c"
        "modules/query/query_command.c"
    INCLUDE_DIRS "."
    REQUIRES bt esp_driver_gpio esp_driver_rmt esp_driver_uart esp_event esp_http_client esp_netif esp_timer esp_wifi esp-zigbee-lib fatfs lwip nvs_flash)
