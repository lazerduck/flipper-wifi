cmake_minimum_required(VERSION 3.20)
project(fuse_radio_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Fetch Unity test framework ────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(unity)

# ── Path shortcuts ────────────────────────────────────────────────────────────
set(STUBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
set(MAIN_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../main)

# ── Common include directories ────────────────────────────────────────────────
# STUBS_DIR must come first so its freertos/*.h shadows the real ESP-IDF headers.
# MAIN_DIR provides types.h, parser.h, router.h, uart.h, led.h, etc.
# wifi/wifi.h is reached as "wifi/wifi.h" relative to MAIN_DIR (same path
# as router.c uses, so no extra include dir is needed).
set(COMMON_INCLUDES
    ${STUBS_DIR}
    ${MAIN_DIR}
)

# ── test_parser — only parser.c compiled from firmware sources ────────────────
add_executable(test_parser
    test_parser.c
    ${MAIN_DIR}/parser.c
)
target_include_directories(test_parser PRIVATE ${COMMON_INCLUDES})
target_link_libraries(test_parser PRIVATE unity)

# ── test_router — router.c + parser.c compiled; all module functions are ──────
# ──             mocked inside test_router.c (wifi.c, ble.c etc. not compiled)
add_executable(test_router
    test_router.c
    ${MAIN_DIR}/router.c
    ${MAIN_DIR}/parser.c
)
target_include_directories(test_router PRIVATE ${COMMON_INCLUDES})
target_link_libraries(test_router PRIVATE unity)

# ── Register with CTest ───────────────────────────────────────────────────────
enable_testing()
add_test(NAME parser_tests COMMAND test_parser)
add_test(NAME router_tests COMMAND test_router)

# ── test_uart — uart.c compiled; ESP-IDF driver and FreeRTOS task functions ──
# ──            are stubbed inside test_uart.c itself                         ──
add_executable(test_uart
    test_uart.c
    ${MAIN_DIR}/uart.c
)
target_include_directories(test_uart PRIVATE ${COMMON_INCLUDES})
target_link_libraries(test_uart PRIVATE unity)
add_test(NAME uart_tests COMMAND test_uart)

# ── test_sd_card — sd_card.c + uart.c + parser.c compiled; ESP-IDF SPI/VFS ──
# ──               and FreeRTOS calls are stubbed in test_sd_card.c; UNIT_TEST ─
# ──               guards sd_init() and esp_vfs_fat_* in sd_card.c             ─
add_executable(test_sd_card
    test_sd_card.c
    ${MAIN_DIR}/sd_card.c
    ${MAIN_DIR}/uart.c
    ${MAIN_DIR}/parser.c
)
target_include_directories(test_sd_card PRIVATE ${COMMON_INCLUDES})
target_link_libraries(test_sd_card PRIVATE unity)
target_compile_definitions(test_sd_card PRIVATE UNIT_TEST)
add_test(NAME sd_card_tests COMMAND test_sd_card)

# ── test_settings — settings.c + uart.c + parser.c compiled; hardware stubs ─
# ──                 and parser/router no-ops are in test_settings.c itself   ─
add_executable(test_settings
    test_settings.c
    ${MAIN_DIR}/settings.c
    ${MAIN_DIR}/uart.c
    ${MAIN_DIR}/parser.c
)
target_include_directories(test_settings PRIVATE ${COMMON_INCLUDES})
target_link_libraries(test_settings PRIVATE unity)
target_compile_definitions(test_settings PRIVATE UNIT_TEST)
add_test(NAME settings_tests COMMAND test_settings)
