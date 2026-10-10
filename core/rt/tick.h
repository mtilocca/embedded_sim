#pragma once
#include <cstdint>

namespace rt {

// Passed to Simulation::step(). dt is the nominal period: the model advances by
// exactly one period of real time per call (fixed-step, no catch-up).
struct Tick {
	uint64_t now_us; // actual start time of this step
	uint32_t dt_us;  // nominal period
	uint32_t seq;    // step counter since start

	float dt_s() const { return static_cast<float>(dt_us) * 1e-6f; }
};

} // namespace rt
