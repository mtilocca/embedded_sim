#include "net/mqtt/payload.h"

#include <zephyr/kernel.h>
#include <cerrno>
#include <cmath>
#include <cstdio>

namespace net::mqtt {
namespace {

const char *quality_str(Quality q)
{
	switch (q) {
	case Quality::Good:
		return "good";
	case Quality::Uncertain:
		return "uncertain";
	default:
		return "bad";
	}
}

} // namespace

int format_json(char *buf, size_t len, const Sample &s)
{
	// JSON has no NaN/Inf: report them as bad quality with value 0.
	const bool finite = std::isfinite(s.value);
	const double v = finite ? static_cast<double>(s.value) : 0.0;
	const Quality q = finite ? s.quality : Quality::Bad;

	int n;
	if (s.unit && s.unit[0]) {
		n = snprintf(buf, len, "{\"v\":%.6g,\"u\":\"%s\",\"q\":\"%s\",\"t\":%u}", v, s.unit,
			     quality_str(q), static_cast<unsigned>(s.uptime_ms));
	} else {
		n = snprintf(buf, len, "{\"v\":%.6g,\"q\":\"%s\",\"t\":%u}", v, quality_str(q),
			     static_cast<unsigned>(s.uptime_ms));
	}
	return (n < 0 || static_cast<size_t>(n) >= len) ? -ENOSPC : n;
}

int format_status(char *buf, size_t len, bool online)
{
	int n = snprintf(buf, len, "{\"state\":\"%s\",\"board\":\"%s\",\"uptime_ms\":%u}",
			 online ? "online" : "offline", CONFIG_BOARD,
			 static_cast<unsigned>(k_uptime_get_32()));
	return (n < 0 || static_cast<size_t>(n) >= len) ? -ENOSPC : n;
}

} // namespace net::mqtt
