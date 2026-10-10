# Architecture and roadmap

Principle: everything runs in real wall-clock time on the MCU, like a real
PLC/RTU. No accelerated or simulated time.

| Step | Area | Folder | Status |
|------|------|--------|--------|
| 1 | Real-time core: periodic tasks, deadlines, jitter stats | `core/rt` | **done, verified on hardware** |
| 2 | MQTT client + payload structuring ([docs/mqtt.md](mqtt.md)) | `net/mqtt` | **done, verified on hardware** |
| 3 | TLS (MQTTS etc.) | `net/tls` | planned |
| 4 | WebSocket backend + telemetry bus ([docs/web.md](web.md)) | `net/ws`, `net/telemetry` | **done, verified on hardware** |
| 5 | Web frontend (TypeScript + Vite, embedded into firmware) | `web/` | **done, verified on hardware** |
| 6 | Input processing: modbus lists -> YAML / C++ headers (Python + Jinja2) | `utils/` | planned |
| 7 | MCU secure boot (MCUboot) | `security/secureboot` | planned |
| 8 | OTA updates | `security/ota` | planned |

Backend for MQTT/WebSocket tests: local Mosquitto on the dev server.

## core/rt

- `Simulation` declares `period_ms()` and `priority()` (defaults: `SIM_DEFAULT_PERIOD_MS`, 7).
- `rt::start()` creates one thread per sim; loop = sleep until absolute deadline, `step()`, advance deadline by one period.
- Overrun: deadlines already missed are skipped and counted (`over=`), never queued.
- `Tick{now_us, dt_us, seq}`: models integrate with fixed `dt` per step.
- Sub-millisecond periods need a fast kernel tick (`CONFIG_SYS_CLOCK_TICKS_PER_SEC`); not set yet, board default applies.

## net/telemetry, net/mqtt, net/ws, web

- Sims call `net::telemetry::publish(sim, signal, Sample)`. Non-blocking, safe from any RT thread; each sink (MQTT, WebSocket) copies into its own queue and drops (counted) when full, so a slow network never stalls a simulation.
- MQTT: one topic per signal, flat JSON, see [mqtt.md](mqtt.md). WebSocket frames reuse the same shape (`{"topic":..,"p":{..}}`).
- The UI lives in the firmware (`web/dist` gzip-embedded; `web/fallback` otherwise): board works on any HIL rig, no server.

## Platform notes and lessons

- ESP32 DRAM is the tight resource (~192 KB). Anything embedded must be `const` so it lands in flash; the 133 KB UI bundle was once copied into RAM by a non-const array.
- Zephyr HTTP server is experimental in 4.5-rc1; its resource macros are C-only, so `net/ws/service.c` is the one C file in the tree.
- Kernel tick is coarse (jitter of 1-5 ms observed on ESP32, `0` on STM32 when idle). Sub-millisecond loops need `CONFIG_SYS_CLOCK_TICKS_PER_SEC` raised; not yet done.
- Real-time nodes must not power-save the radio (ESP32 modem sleep drops traffic).
- Wi-Fi coprocessor on STM32 needs one-time T02 firmware (locks the module): [stm32-wifi.md](stm32-wifi.md).
