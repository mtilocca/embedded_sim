# Device-hosted web UI

The UI lives in the firmware, so a flashed board works on any network/HIL rig with no server.

```
browser ──HTTP──► board:80   /  /app.js  /app.css   (gzip, embedded in flash)
browser ──WS────► board:80   /ws                    live telemetry (primary source)
browser ──WS────► broker:9001                       optional second source (MQTT over WebSocket)
```

## Live protocol (`/ws`, device -> browser, text frames)

Same shape as an MQTT message, so both sources are handled identically:

```json
{"topic":"embedded_sim/stm32-01/sensors/demo_temp","p":{"v":20.3,"u":"degC","q":"good","t":123456}}
{"topic":"embedded_sim/stm32-01/status","p":{"state":"online","board":"nucleo_u575zi_q","uptime_ms":99000}}
```

Status is sent every second as a heartbeat. Browser -> device commands are not implemented yet (planned: `.../set`).
If a sample arrives from both sources, duplicates are dropped by device-uptime `t`.

## Firmware

- `net/telemetry.*`: bus; sims call `net::telemetry::publish(sim, signal, Sample)`; sinks: MQTT, WebSocket.
- `net/ws/service.c`: Zephyr HTTP service + embedded files (C because of Zephyr macros). `net/ws/ws.cpp`: client table + fan-out thread.
- Files embedded: `web/dist/{index.html,app.js,app.css}`, or `web/fallback/` if `web/dist` is not built.

## Frontend (`web/`, TypeScript + Vite, deps: uPlot, mqtt.js)

Tabs: Overview (live tiles), Charts (one per signal), Real-time (task jitter/exec/overruns), Console (raw frames, filter/pause), Settings (optional broker).

```sh
sudo apt install -y nodejs npm      # once
scripts/web-build.sh                # -> web/dist, then rebuild the firmware to embed it
scripts/web-dev.sh 192.168.1.45     # live-reload UI against a real board
```

## Mosquitto WebSocket listener (only for the optional broker source)

Add to `~/stack/mosquitto/config/mosquitto.conf` and publish the port (`-p 9001:9001` in `mosquitto.sh`'s `docker run`):

```
listener 9001
protocol websockets
```
