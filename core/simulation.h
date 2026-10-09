#pragma once
#include <cstdint>

namespace sim {

// One simulation domain (sensors, scada, ...). Cooperative: step() is called
// every CONFIG_SIM_TICK_MS from the main loop and must not block.
class Simulation {
public:
	virtual ~Simulation() = default;
	virtual const char *name() const = 0;
	virtual int init() = 0;
	virtual void step(uint32_t now_ms) = 0;
};

} // namespace sim
