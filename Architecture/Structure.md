# Firmware Structure

## Directory layout

```
main/
├── main.c              Entry point — init sequence and main loop
├── uart.c/h            UART send/receive
├── parser.c/h          Tokenises incoming lines into command + args
├── router.c/h          Walks command tokens, dispatches to modules
├── led.c/h             WS2812 LED driver
├── sd_card.c/h         SD card mount/read/write
├── settings.c/h        Settings read/write (depends on sd_card)
├── wifi/
│   ├── wifi.c/h        WiFi module — init/deinit, owns mode state
│   ├── promiscuous.c   Promiscuous sub-module (channel scan, device scan)
│   ├── connected.c     Connected sub-module (IP sweep, internet services)
│   └── beacon.c        Beacon spam sub-module
├── ble/
│   ├── ble.c/h         BLE module — init/deinit
│   ├── scan.c          BLE scan sub-module
│   ├── gatt.c          GATT inspect sub-module
│   └── rssi_track.c    RSSI tracking sub-module
└── zigbee/
    ├── zigbee.c/h      ZigBee module — init/deinit
    ├── scan.c          Channel scan sub-module
    └── listen.c        Packet listen sub-module
```

---

## Module structure

```mermaid
graph TD
    subgraph Boot
        main["main.c\nentry point"]
    end

    subgraph Core["Core (always loaded)"]
        uart["uart.c\nUART send/receive"]
        parser["parser.c\ntokeniser"]
        router["router.c\ndispatcher"]
        led["led.c\nWS2812"]
        sd["sd_card.c"]
        settings["settings.c"]
    end

    subgraph WiFi
        wifi["wifi.c\ninit / deinit"]
        wprom["promiscuous.c"]
        wconn["connected.c"]
        wbeac["beacon.c"]
    end

    subgraph BLE
        ble["ble.c\ninit / deinit"]
        bscan["scan.c"]
        bgatt["gatt.c"]
        brssi["rssi_track.c"]
    end

    subgraph ZigBee
        zbee["zigbee.c\ninit / deinit"]
        zscan["scan.c"]
        zlisten["listen.c"]
    end

    main --> uart & led & sd & settings & router
    sd --> settings
    uart --> parser --> router

    router -->|"WIFI_..."| wifi
    router -->|"BLE_..."| ble
    router -->|"ZIGBEE_..."| zbee

    wifi --> wprom & wconn & wbeac
    ble  --> bscan & bgatt & brssi
    zbee --> zscan & zlisten

    led  -.->|"status updates"| wifi & ble & zbee
    settings -.->|"config reads"| wifi & ble & zbee
```

---

## Command routing

Commands are routed by progressively consuming `_`-delimited tokens from the command name.
Each layer consumes only its own token and passes the remainder down — so depth is not hardcoded.

```mermaid
graph LR
    raw["WIFI_PROMISCUOUS_SCAN\nchannels='1|6|11'"]

    parser["parser.c\nsplit into tokens + args\n─────────────\ntokens: WIFI, PROMISCUOUS, SCAN\nargs: channels=1|6|11"]

    router["router.c\nconsumes: WIFI\npasses: PROMISCUOUS, SCAN + args\n─────────────\nlooks up wifi module"]

    wifi["wifi.c\nconsumes: PROMISCUOUS\npasses: SCAN + args\n─────────────\nlooks up promiscuous sub-module"]

    prom["promiscuous.c\nconsumes: SCAN\nreads args directly"]

    raw --> parser --> router --> wifi --> prom
```

The same mechanism handles system commands — `router.c` handles `SYS_*` and `LED_*` directly without delegating to a sub-module, since those have no further depth.

---

## Shared context

Core services (LED, SD, settings, UART send) are accessible as module-level singletons initialised in `main.c`. Radio modules call them directly rather than having them injected — appropriate for single-threaded embedded firmware.
