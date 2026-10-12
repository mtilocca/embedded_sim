# Architecture (as built)

Everything on this page exists in the repo and has been verified on both boards
(NUCLEO-U575ZI-Q + X-NUCLEO-67W61M1, ESP32-DevKitC) unless marked otherwise.
Planned work lives in [roadmap.md](roadmap.md).

Principle: everything runs in **real wall-clock time** on the MCU, like a real
PLC/RTU. No accelerated or Simulink-style time.

Contents: [System](#1-system-overview) · [Boot](#2-boot-sequence) ·
[Real-time core](#3-real-time-core) · [Telemetry bus](#4-telemetry-bus-and-threads) ·
[MQTT](#5-mqtt-client) · [Topics](#6-topic-and-payload-model) ·
[Web UI](#7-web-ui) · [Build pipeline](#8-build-and-flash-pipeline) ·
[Memory](#9-memory-layout) · [Lessons](#10-platform-notes-and-lessons)

## 1. System overview

```mermaid
flowchart LR
    subgraph boards["Boards on the LAN (static IPs)"]
        stm["STM32U575<br/>stm32-01<br/>192.168.1.45<br/>Wi-Fi 6 via ST67W611M1 (SPI)"]
        esp["ESP32<br/>esp32-01<br/>192.168.1.44<br/>native Wi-Fi"]
    end
    subgraph server["Dev server 192.168.1.147 (Docker)"]
        mosq[("Mosquitto<br/>:1883 auth")]
        influx[("InfluxDB 2<br/>:8086")]
        graf["Grafana OSS<br/>:3000"]
    end
    user(["Browser"])

    stm -- "MQTT telemetry" --> mosq
    esp -- "MQTT telemetry" --> mosq
    user -- "HTTP + WebSocket<br/>UI served by the board" --> stm
    user -- "HTTP + WebSocket" --> esp
    mosq -. "planned: Telegraf" .-> influx --> graf
    mosq -. "optional: MQTT over WebSocket :9001" .-> user
```

Solid lines work today. The boards are self-sufficient: the UI and live data
come straight from the board, so the server is optional (it only adds history
and dashboards).

## 2. Boot sequence

```mermaid
sequenceDiagram
    participant M as main()
    participant W as net::wifi_connect
    participant S as Simulation[i]
    participant T as net/telemetry
    participant MQ as net::mqtt::start
    participant R as rt::start
    M->>W: join SSID, static IPv4 (30 s timeout)
    W-->>M: connected (or continue offline)
    loop each enabled sim
        M->>S: init()
        Note over S,T: webserver.init() starts the HTTP server,<br/>the WebSocket thread, and registers its sink
    end
    M->>MQ: register MQTT sink, start client thread
    M->>R: one periodic thread per sim
    loop every SIM_STATS_PERIOD_S
        M->>T: publish rt stats (jitter, exec, overruns)
    end
```

Sinks are registered before the real-time threads start, so the sink list is
never modified while sims are publishing.

## 3. Real-time core

`core/rt` runs each simulation in its own thread on absolute deadlines.

```mermaid
flowchart TD
    A["next = now + period"] --> B["sleep until absolute deadline 'next'"]
    B --> C["jitter = start - next"]
    C --> D["sim.step(Tick: now_us, dt_us, seq)"]
    D --> E["exec = end - start<br/>update max/avg stats"]
    E --> F["next += period"]
    F --> G{"end >= next?<br/>(step overran)"}
    G -- no --> B
    G -- yes --> H["overruns++<br/>skip missed deadlines<br/>(never queue them up)"] --> B
```

| Rule | Why |
|------|-----|
| Deadlines advance by exactly one period | no drift, like a PLC scan cycle |
| Missed deadlines are skipped and counted | a slow step cannot cause a burst of catch-up steps |
| `dt` is the nominal period | models integrate with a fixed step in real time |
| Each sim declares `period_ms()` and `priority()` | fast loops (robotics) and slow ones (SCADA) coexist |

Measured so far (empty sims): `over=0`; jitter 0-6 ms, dominated by the kernel
tick. Sub-millisecond loops need a faster tick (see [roadmap](roadmap.md)).

## 4. Telemetry bus and threads

Simulations never talk to MQTT or the web UI. They call one function; sinks
decide where the data goes.

```mermaid
classDiagram
    class Simulation {
        <<interface>>
        +name() const char*
        +init() int
        +step(Tick) void
        +period_ms() uint32
        +priority() int
    }
    class Registry {
        +enabled() list
    }
    class Runner {
        +start(sims, n) int
        +get_task(i) stats
        +log_stats()
    }
    class Telemetry {
        +add_sink(Sink)
        +publish(sim, signal, Sample)
    }
    class MqttClient {
        -queue
        +offer(Message)
    }
    class WsFanout {
        -queue
        -clients
        +offer(Message)
    }
    Simulation <|-- Sensors
    Simulation <|-- Scada
    Simulation <|-- Robotics
    Simulation <|-- Pumps
    Simulation <|-- WebServer
    Registry --> Simulation : Kconfig-enabled list
    Runner --> Simulation : one thread each
    Simulation ..> Telemetry : publish
    Telemetry --> MqttClient : sink
    Telemetry --> WsFanout : sink
```

```mermaid
flowchart LR
    subgraph threads["Threads"]
        t1["sim threads<br/>prio 7, periodic"]
        t2["mqtt thread<br/>prio 9"]
        t3["ws thread<br/>prio 9"]
        t4["HTTP server thread"]
        t5["main: stats every 10 s"]
    end
    t1 -- "publish: non-blocking,<br/>copies into each sink queue" --> q1[("MQTT queue 16")]
    t1 --> q2[("WS queue 16")]
    q1 --> t2
    q2 --> t3
    t5 -- "rt stats" --> q1
    t5 --> q2
    t3 -- "text frames" --> t4
```

A full queue drops the sample and counts it. A slow network can never stall a
simulation thread.

## 5. MQTT client

```mermaid
stateDiagram-v2
    [*] --> Connecting
    Connecting --> Backoff: TCP/connect failed
    Backoff --> Connecting: wait 5 s
    Connecting --> WaitConnack: socket open
    WaitConnack --> Backoff: refused (e.g. not authorised) or 5 s timeout
    WaitConnack --> Online: CONNACK ok
    Online --> Online: drain queue and publish QoS0, poll input 20 ms, ping at half the keepalive
    Online --> Reconnect: socket error / broker closed
    Reconnect --> Connecting: abort, wait 2 s
```

On connect the client publishes a retained `online` status. The broker
publishes the retained `offline` Last Will if the board vanishes.

## 6. Topic and payload model

Full spec: [mqtt.md](mqtt.md).

```mermaid
flowchart LR
    root["embedded_sim"] --> dev1["stm32-01"]
    root --> dev2["esp32-01"]
    dev1 --> st["status<br/>retained, Last Will"]
    dev1 --> sensors["sensors/demo_temp"]
    dev1 --> rt["rt/<br/>task_jitter_max<br/>task_exec_avg<br/>task_overruns"]
    dev1 --> future["scada/..., robotics/..., pumps/...<br/>(models not built yet)"]
    sensors --> p["payload: v, u, q, t<br/>{&quot;v&quot;:20.3,&quot;u&quot;:&quot;degC&quot;,&quot;q&quot;:&quot;good&quot;,&quot;t&quot;:123456}"]
```

One topic per signal, flat JSON. Telegraf can take `device`, `sim` and `signal`
tags straight from the topic. The device has no RTC, so `t` is uptime and the
ingester stamps wall-clock time.

## 7. Web UI

The UI lives **in the firmware**, so a flashed board works on any network or HIL
rig with no server.

```mermaid
flowchart TD
    subgraph fw["Firmware (flash)"]
        files["index.html + app.js + app.css<br/>gzip, const arrays"]
        http["Zephyr HTTP server<br/>6 slots, 3 s inactivity timeout"]
        ws["/ws WebSocket<br/>frames {topic, p}"]
        files --> http
    end
    subgraph ui["Browser: web/src (TypeScript)"]
        src1["sources.ts<br/>device WebSocket (always)"]
        src2["sources.ts<br/>MQTT over WebSocket (optional)"]
        store["store.ts<br/>signals, history, devices, log<br/>dedupe by device uptime t"]
        v1["Overview"]
        v2["Charts (uPlot)"]
        v3["Real-time"]
        v4["Console"]
        v5["Settings"]
    end
    http -- "GET /, /app.js, /app.css" --> ui
    ws --> src1
    broker[("Mosquitto :9001")] -.-> src2
    src1 --> store
    src2 --> store
    store --> v1
    store --> v2
    store --> v3
    store --> v4
    v5 -. "broker URL, user, password (this browser only)" .-> src2
```

Screenshots of every tab: [web.md](web.md#tour-of-the-ui).

| Tab | Shows |
|-----|-------|
| Overview | live tile per signal, device online/offline, board, uptime |
| Charts | one live chart per numeric signal (600 points) |
| Real-time | per-task jitter, exec time, overruns (red when a deadline was missed) |
| Console | raw frames with filter, pause, clear |
| Settings | optional MQTT broker as a second source |

## 8. Build and flash pipeline

```mermaid
flowchart LR
    ts["web/src/*.ts + css"] -- "scripts/web-build.sh<br/>tsc + vite" --> dist["web/dist<br/>index.html app.js app.css"]
    fb["web/fallback<br/>(tiny page)"] -. "used if dist is missing" .-> gz
    dist -- "CMake: gzip to .inc" --> gz["build/.../generated/*.gz.inc"]
    gz --> svc["net/ws/service.c<br/>static const arrays"]
    kcfg["prj.conf + boards/*.conf<br/>+ secrets.conf"] --> fwb
    svc --> fwb["scripts/build.sh stm32 or esp32<br/>west build"]
    shield["SHIELD=x_nucleo_67w61m1<br/>(stm32 only)"] --> fwb
    fwb -- "scripts/flash.sh" --> hw["pyOCD (STM32)<br/>esptool (ESP32)"]
    hw --> mon["scripts/monitor.sh"]
```

## 9. Memory layout

Measured on 2026-10-10 with the UI embedded. The ESP32's RAM is the scarce
resource, not flash.

```mermaid
flowchart LR
    subgraph esp["ESP32 (4 MB flash, about 192 KB + 93 KB DRAM)"]
        ef["Flash 18%:<br/>code + const UI (about 135 KB gz)"]
        ed0["DRAM0 55%:<br/>kernel, stacks, net buffers"]
        ed1["DRAM1 86%:<br/>Wi-Fi driver, heap"]
    end
    subgraph stm["STM32U575 (2 MB flash, 768 KB RAM)"]
        sf["Flash about 15%:<br/>code + const UI"]
        sr["RAM about 30%"]
    end
```

Rule: anything embedded must be `static const` so it lands in flash. A
non-`const` copy of the UI once pushed the ESP32 over its DRAM limit.

## 10. Platform notes and lessons

| Topic | Note |
|-------|------|
| ESP32 RAM | keep embedded files `const`; inspect with `ninja -C build/esp32 ram_report` |
| Wi-Fi power save | off on ESP32 (`ESP32_WIFI_STA_POWER_SAVE_NONE`): modem sleep drops ARP/TCP |
| First boot after flashing | ESP32 may need one press of EN before it is reachable (cause not found) |
| HTTP server | experimental in Zephyr 4.5-rc1; macros are C-only, so `net/ws/service.c` is the one C file |
| Browser connections | parallel keep-alive + WebSocket need `HTTP_SERVER_MAX_CLIENTS=6` |
| MQTT keepalive | `mqtt_live()` returns `-EAGAIN` when no ping is due; not an error |
| STM32 Wi-Fi | ST67W611M1 needs one-time T02 firmware (locks the module): [stm32-wifi.md](stm32-wifi.md) |
| Kernel tick | coarse; jitter of 1-5 ms observed on ESP32 |
| WebSocket viewers | Zephyr default is ONE websocket system-wide: extra viewers were upgraded then closed at once. `WEBSOCKET_MAX_CONTEXTS=8`, `SIM_WS_MAX_CLIENTS=4`; when full, `net/ws/ws.cpp` evicts the oldest viewer (stale tabs/phones that left the Wi-Fi are only noticed when a send fails) |
