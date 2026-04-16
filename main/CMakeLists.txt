idf_component_register(
    SRCS
        "main.c"
        "command/command_router.c"
        "modules/ping/ping_command.c"
        "modules/wifi/wifi_command.c"
        "modules/wifi/wifi_manager.c"
        "modules/send/send_command.c"
        "modules/query/query_command.c"
    INCLUDE_DIRS ".")