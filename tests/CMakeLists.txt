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
