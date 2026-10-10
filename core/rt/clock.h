#pragma once
#include <zephyr/kernel.h>
#include <cstdint>

namespace rt {

// Monotonic wall-clock time since boot. Simulations never see accelerated time.
inline uint64_t now_us()
{
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

} // namespace rt
