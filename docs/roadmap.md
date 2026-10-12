# Roadmap (planned)

Everything here is a **proposal**, not built. What exists today is in
[architecture.md](architecture.md). Each step is done one at a time, with open
questions settled with the project owner before code is written.

## 1. Where we are and what comes next

```mermaid
flowchart LR
    subgraph done["Done and verified on hardware"]
        d1["Wi-Fi on both boards"]
        d2["Real-time core"]
        d3["Telemetry bus"]
        d4["MQTT client + topic and payload convention"]
        d5["Device-hosted web UI + WebSocket"]
    end
    subgraph next["Next"]
        n1["utils/ codegen<br/>(modbus list to YAML + header)"]
        n2["Real simulation models<br/>pumps first"]
        n3["Telegraf to InfluxDB to Grafana"]
        n4["UI to device commands (/set)"]
    end
    subgraph later["Later"]
        l1["net/tls: MQTTS, HTTPS, WSS"]
        l2["security/secureboot"]
        l3["security/ota"]
        l4["Tests and CI"]
    end
    done --> n1 --> n2
    done --> n3
    d5 --> n4
    n2 --> n4
    d4 --> l1
    l1 --> l3
    l2 --> l3
    n2 --> l4
```

Order of work, with the reason for it:

| # | Step | Folder | Why this order |
|---|------|--------|----------------|
| 1 | Input processing (codegen) | `utils/` | every real device (pump, SCADA point list) starts as a file; without it we hard-code each device |
| 2 | Real sim models | `sims/pumps` first | proves the codegen, gives real data for the UI and dashboards |
| 3 | Dashboards pipeline | server side | Telegraf mapping is already specified in [mqtt.md](mqtt.md) |
| 4 | Commands from the UI | `net/ws`, `web/` | needs real models to be worth controlling |
| 5 | TLS | `net/tls` | MQTTS, then HTTPS/WSS; a prerequisite for a trustworthy OTA |
| 6 | Secure boot | `security/secureboot` | signed images, per board |
| 7 | OTA | `security/ota` | needs secure boot and TLS first |
| 8 | Tests and CI | `tests/` | host-side tests for payload, runner maths, codegen |

## 2. Step 1: input processing (`utils/`)

Goal: add a new device by dropping in its file, not by editing C++. Python with
Jinja2 templates (decided).

```mermaid
flowchart TD
    in1["Modbus register list<br/>CSV, XLSX or JSON"] --> parse
    in2["Other point lists<br/>(SCADA tags, sensor specs)"] --> parse
    parse["utils/codegen<br/>parse and validate<br/>against a schema"] --> yaml["device.yaml<br/>normalised, human-editable,<br/>the single source of truth"]
    yaml --> t1["Jinja2: C++ header<br/>registers, types, scaling"]
    yaml --> t2["Jinja2: UI metadata<br/>labels, units, limits"]
    yaml --> t3["Jinja2: Grafana / Telegraf<br/>config (optional)"]
    t1 --> fw["sims/pumps<br/>compiled into firmware"]
    t2 --> web["web/ signal catalogue"]
    t3 --> srv["server stack"]
```

Open questions:
- What format does the first real modbus list come in (CSV, XLSX, JSON, a vendor PDF table), and which pump is it? One real example file decides the schema.
- Should the register map also drive a **Modbus TCP server on the board**, so SCADA clients can poll the simulated pump? (not decided; would add a `net/modbus/` module)
- Generated headers: committed to git, or generated at build time? (proposal: at build time, inputs in git)

## 3. Step 2: real simulation models

```mermaid
flowchart TD
    yaml["device.yaml"] --> gen["generated header"]
    gen --> pump["sims/pumps<br/>period e.g. 10 ms"]
    pump -- "flow, pressure, speed, state" --> bus["telemetry bus"]
    pump -- "reads setpoints" --> cmd["command inbox<br/>(from /set, step 4)"]
    scada["sims/scada<br/>period e.g. 100 ms"] -- "reads" --> bus2["shared signal table"]
    pump --> bus2
    bus --> out["MQTT + WebSocket + UI"]
```

Open questions: process models (which equations / PLC-style logic), the periods
each sim needs, and whether sims exchange values on the board (a shared signal
table) or only through MQTT.

## 4. Step 3: dashboards pipeline

```mermaid
flowchart TD
    boards["boards"] -- "embedded_sim/#" --> mosq[("Mosquitto")]
    mosq --> tele["Telegraf<br/>mqtt_consumer + json_v2<br/>topic_parsing: device, sim, signal"]
    tele --> influx[("InfluxDB 2<br/>bucket: embedded_sim")]
    influx --> graf["Grafana OSS<br/>provisioned dashboards"]
```

Tags come from the topic, so no firmware change is needed. Open question: should
the compose file and provisioned dashboards live in this repo (`deploy/`)?

## 5. Step 4: commands from the UI

```mermaid
sequenceDiagram
    participant U as Browser
    participant D as Board
    participant S as Simulation
    U->>D: WebSocket frame {topic: .../pump1_speed/set, p: {v: 1450}}
    D->>D: validate: known signal, range, writable
    D->>S: write into the sim's command inbox
    S-->>D: new value applied on its next step
    D-->>U: telemetry frame shows the new value
```

The same `.../set` topic would also be accepted over MQTT. Open question:
authorisation. Without TLS and a login, anyone on the LAN could drive the
simulation (see step 5).

## 6. Step 5: TLS (`net/tls`)

```mermaid
flowchart LR
    subgraph board["Board"]
        creds["Zephyr TLS credentials<br/>CA cert, device cert, key<br/>(flash, later secure storage)"]
        mqtts["MQTTS client"]
        https["HTTPS + WSS server"]
        creds --> mqtts
        creds --> https
    end
    mqtts -- "8883" --> mosq[("Mosquitto TLS listener")]
    https -- "443" --> br["Browser"]
```

Open questions: own CA (a script in `utils/`) versus self-signed per device; one
cert per device or a shared one; whether HTTPS is wanted on the board at all
(browsers then warn about a private CA), or only MQTTS.

## 7. Steps 6 and 7: secure boot and OTA

Proposal: MCUboot (Zephyr's standard bootloader) on both boards, signed images.

```mermaid
flowchart LR
    subgraph flash["Flash layout (proposed)"]
        boot["MCUboot<br/>holds the public key"]
        s0["Slot 0<br/>running image"]
        s1["Slot 1<br/>staged update"]
        st["Storage<br/>config, certs"]
    end
    boot --> s0
    s1 -. "swap on reboot if signature valid" .-> s0
```

```mermaid
sequenceDiagram
    participant Dev as Developer
    participant Sign as imgtool
    participant Srv as Update server
    participant B as Board
    Dev->>Sign: sign firmware with the private key
    Sign-->>Srv: signed image + version
    Srv-->>B: announces new version (MQTT) or board polls
    B->>Srv: downloads over TLS into slot 1
    B->>B: mark image pending, reboot
    B->>B: MCUboot verifies signature, swaps
    alt new image is healthy
        B->>B: confirm image
    else crash or no confirm
        B->>B: MCUboot reverts to the old image
    end
```

Open questions:
- Key handling: where the private signing key lives and who may sign.
- Trigger: pushed from the UI, announced over MQTT, or polled by the board.
- STM32U5 TrustZone and ESP32 flash encryption: use them or keep it to signature checks only.
- The ST67W611M1 module has its own signed firmware (T02, locked); it is updated separately from the host image ([stm32-wifi.md](stm32-wifi.md)).

## 8. Smaller items

| Item | Detail |
|------|--------|
| Kernel tick | raise `SYS_CLOCK_TICKS_PER_SEC` if a sim needs periods under 1 ms; measure jitter before and after |
| ESP32 first-boot reachability | after flashing, EN must be pressed once; root cause not found |
| ESP32 RAM headroom | DRAM1 is at 86%; TLS and real models will need budget, check with `ram_report` |
| `mosquitto.sh` | re-running `install` drops the WebSocket port 9001 mapping |
| MQTT sample drops | publish `dropped()` counts as an `rt` signal |
| Weak MQTT password | `embedded_sim` uses a trivial password; change it before exposing the broker |
| Tests | host-side unit tests (`native_sim`) for payload formatting and the runner's deadline arithmetic |
