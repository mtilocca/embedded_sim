#include "net/telemetry.h"

#include <zephyr/kernel.h>
#include <cstring>

namespace net::telemetry {
namespace {

constexpr size_t kMaxSinks = 4;
Sink sinks[kMaxSinks];
size_t sink_count;

} // namespace

int add_sink(Sink sink)
{
	if (sink_count >= kMaxSinks) {
		return -ENOMEM;
	}
	sinks[sink_count++] = sink; // called during init, before sims run
	return 0;
}

int publish(const char *sim, const char *signal, const mqtt::Sample &sample)
{
	Message m = {};
	strncpy(m.sim, sim, sizeof(m.sim) - 1);
	strncpy(m.signal, signal, sizeof(m.signal) - 1);
	if (sample.unit) {
		strncpy(m.unit, sample.unit, sizeof(m.unit) - 1);
	}
	m.sample = sample;
	m.sample.unit = nullptr;
	if (m.sample.uptime_ms == 0) {
		m.sample.uptime_ms = k_uptime_get_32();
	}

	int ret = 0;
	for (size_t i = 0; i < sink_count; i++) {
		if (sinks[i](m) != 0) {
			ret = -ENOMEM; // at least one sink dropped it
		}
	}
	return ret;
}

} // namespace net::telemetry
