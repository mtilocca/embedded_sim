#pragma once
#include "net/mqtt/payload.h"

// Transport-independent telemetry bus. Simulations call publish(); every
// registered sink (MQTT client, WebSocket fan-out, ...) gets its own copy.
// Never blocks, safe from any thread. Without sinks it is a no-op.

namespace net::telemetry {

struct Message {
	char sim[16];
	char signal[24];
	char unit[12]; // owned copy: the caller's string may not outlive the queue
	mqtt::Sample sample; // sample.unit is not valid in a copy, use view()

	mqtt::Sample view() const
	{
		mqtt::Sample s = sample;
		s.unit = unit[0] ? unit : nullptr;
		return s;
	}
};

// Sink callback: copy the message into its own queue. Must not block.
using Sink = int (*)(const Message &);

#ifdef CONFIG_SIM_TELEMETRY
int add_sink(Sink sink);
int publish(const char *sim, const char *signal, const mqtt::Sample &sample);
#else
inline int add_sink(Sink) { return 0; }
inline int publish(const char *, const char *, const mqtt::Sample &) { return 0; }
#endif

} // namespace net::telemetry
