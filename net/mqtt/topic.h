#pragma once
#include <cstddef>
#include <cstdio>

// Topic convention, see docs/mqtt.md:
//   embedded_sim/<device>/<sim>/<signal>        telemetry   (QoS0, not retained)
//   embedded_sim/<device>/<sim>/<signal>/set    command     (device subscribes)
//   embedded_sim/<device>/status                state       (QoS0, retained, LWT)
// Segments are [a-z0-9_-] only: no '/', '+', '#', spaces.

namespace net::mqtt::topic {

inline constexpr const char *kRoot = "embedded_sim";

inline int status(char *buf, size_t len)
{
	return snprintf(buf, len, "%s/%s/status", kRoot, CONFIG_SIM_DEVICE_ID);
}

inline int telemetry(char *buf, size_t len, const char *sim, const char *signal)
{
	return snprintf(buf, len, "%s/%s/%s/%s", kRoot, CONFIG_SIM_DEVICE_ID, sim, signal);
}

} // namespace net::mqtt::topic
