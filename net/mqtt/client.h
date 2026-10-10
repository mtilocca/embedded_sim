#pragma once
#include "net/mqtt/payload.h"

namespace net::mqtt {

// Register as a telemetry sink and start the MQTT thread (connects to
// CONFIG_SIM_MQTT_BROKER, reconnects forever). Samples published through
// net/telemetry.h go to <root>/<device>/<sim>/<signal>; full queue = dropped.
int start();

bool connected();

uint32_t dropped();

} // namespace net::mqtt
