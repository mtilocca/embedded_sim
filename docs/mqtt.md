# MQTT convention

## Topics

```
embedded_sim/<device>/<sim>/<signal>        telemetry  QoS0, not retained
embedded_sim/<device>/<sim>/<signal>/set    command    device subscribes (planned)
embedded_sim/<device>/status                state      QoS0, retained; also the Last Will
```

- `<device>`: `CONFIG_SIM_DEVICE_ID` (`stm32-01`, `esp32-01`). Also the MQTT client id.
- `<sim>`: `sensors`, `scada`, `robotics`, `pumps`, or the reserved `rt` (real-time task health).
- `<signal>`: one measurement, e.g. `demo_temp`, `pump1_flow`, `rt/..._jitter_max`.
- Segments: lowercase `[a-z0-9_-]`, never `/ + #` or spaces. Max 15 / 23 chars for sim / signal.
- One topic per signal, so Influx/Grafana get tags straight from the topic and a UI can subscribe to just what it shows.

## Payload (JSON, flat)

```json
{"v":20.31,"u":"degC","q":"good","t":123456}
```

| key | meaning |
|-----|---------|
| `v` | numeric value (NaN/Inf never sent; sent as `0` with `q:"bad"`) |
| `u` | unit, optional |
| `q` | `good` / `uncertain` / `bad` |
| `t` | device uptime in ms. No RTC on the boards, so ingestion stamps wall-clock time |

Status: `{"state":"online","board":"nucleo_u575zi_q","uptime_ms":1234}`; the Last Will publishes `"state":"offline"`.

Real-time health, `embedded_sim/<device>/rt/<task>_<metric>` with metric
`jitter_max` (us), `exec_avg` (us), `overruns` (count); every `SIM_STATS_PERIOD_S`.

## Ingestion (InfluxDB 2 / Grafana OSS)

Telegraf `mqtt_consumer` extracts tags from the topic, so no firmware changes are needed:

```toml
[[inputs.mqtt_consumer]]
  servers = ["tcp://mosquitto:1883"]
  topics  = ["embedded_sim/+/+/+"]
  data_format = "json_v2"
  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "embedded_sim/+/+/+"
    measurement = "_/_/_/measurement"   # signal
    tags = "_/device/sim/_"
  [[inputs.mqtt_consumer.json_v2]]
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "v"
      rename = "value"
      type = "float"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "u"
      rename = "unit"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "q"
      rename = "quality"
```

(Telegraf is not deployed yet; this is the target mapping, to be verified when added.)

## Firmware

`net/mqtt/`: `topic.h` (builders), `payload.*` (Sample -> JSON), `client.*`
(thread, auto-reconnect, non-blocking `publish()` safe from any sim thread).
Plain TCP for now; TLS comes with `net/tls`. Credentials: `CONFIG_SIM_MQTT_USER/PASSWORD` in `secrets.conf`.
