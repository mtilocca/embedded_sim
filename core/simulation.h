#pragma once
#include "core/rt/tick.h"
#include <cstdint>

namespace sim {

// One simulation domain (sensors, scada, ...). The real-time core (core/rt)
// runs each one in its own thread, calling step() every period_ms() of wall-clock
// time. step() must finish well within its period and must not block for long.
class Simulation {
public:
	virtual ~Simulation() = default;
	virtual const char *name() const = 0;
	virtual int init() = 0;
	virtual void step(const rt::Tick &tick) = 0;

	virtual uint32_t period_ms() const { return CONFIG_SIM_DEFAULT_PERIOD_MS; }
	// Preemptible thread priority: 0 = most urgent .. 14.
	virtual int priority() const { return 7; }
};

} // namespace sim
