#pragma once
#include <cstddef>
#include <cstdint>

namespace net::mqtt {

enum class Quality { Good, Uncertain, Bad };

// One measurement. Wire format (flat JSON, Telegraf/Influx/Grafana friendly):
//   {"v":12.5,"u":"bar","q":"good","t":123456}
// v = value, u = unit (optional), q = quality, t = device uptime in ms.
// The device has no wall clock, so the ingester stamps arrival time.
struct Sample {
	float value = 0;
	const char *unit = nullptr; // [A-Za-z0-9_%/.-] only, no JSON escaping is done
	Quality quality = Quality::Good;
	uint32_t uptime_ms = 0;
};

// Returns bytes written (excluding NUL) or -ENOSPC if it does not fit.
int format_json(char *buf, size_t len, const Sample &s);

// {"state":"online","fw":"<board>","ip":"..."} style status payloads.
int format_status(char *buf, size_t len, bool online);

} // namespace net::mqtt
