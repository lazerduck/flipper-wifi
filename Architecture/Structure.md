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

---

## State design

All runtime state lives in a single `app_state_t` struct, initialised in `main.c` and passed by pointer to every module that needs it. This gives a single source of truth, makes cancellation trivial, and lets sub-modules cache their own results cleanly.

### Struct hierarchy

```mermaid
classDiagram
    class app_state_t {
        app_mode_t mode
        active_op_t *active_op
        wifi_state_t wifi
        ble_state_t ble
        zigbee_state_t zigbee
        sd_state_t sd
    }

    class active_op_t {
        TaskHandle_t task
        volatile bool stop_requested
    }

    class wifi_state_t {
        wifi_sub_mode_t sub_mode
        ssid_entry_t *scan_results
        uint8_t ssid_count
        char connected_ip[16]
        char connected_mac[18]
    }

    class ble_state_t {
        ble_device_t *scan_results
        uint8_t device_count
        char tracked_mac[18]
    }

    class zigbee_state_t {
        uint8_t active_channel
        channel_stats_t *channel_stats
        uint8_t channel_count
    }

    class sd_state_t {
        bool present
        bool mounted
    }

    app_state_t --> active_op_t : active_op*
    app_state_t --> wifi_state_t
    app_state_t --> ble_state_t
    app_state_t --> zigbee_state_t
    app_state_t --> sd_state_t
```

### `active_op` — cancellation

When a long-running operation starts (stream or silent background), it allocates an `active_op_t`, stores its own FreeRTOS task handle in it, and sets `app_state->active_op`. The task loops checking `active_op->stop_requested`.

`SYS_STOP` handling becomes:
```c
if (state->active_op) {
    state->active_op->stop_requested = true;
    // task detects flag, sends STOPPED\n, cleans up, sets active_op = NULL
}
```

No forced task deletion — the task exits on its own terms, ensuring resources (buffers, radio state) are released cleanly.

If `active_op` is `NULL` when `SYS_STOP` arrives, it is silently ignored.

### Radio sub-states — cached results

Each radio sub-state owns its results. Scan results persist in the sub-state until the next scan overwrites them, so the Flipper can re-request data from a completed scan without triggering a new one.

The `ERR_WRONG_MODE` check at the router level means a radio's sub-state is only ever touched while that radio is active — no cross-contamination between modes.

### Preventing double-init

Before `SYS_MODE_SET` initialises a radio, it checks `state->mode`. If a mode is already active it returns `ERR_BUSY` rather than attempting to init on top of an existing radio stack. The only clean way to switch modes is `SYS_RESTART`.
