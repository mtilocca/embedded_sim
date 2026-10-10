#pragma once
#include "core/rt/stats.h"
#include <cstddef>
#include <cstdint>

namespace sim {
class Simulation;
}

namespace rt {

// Start one periodic thread per simulation (period/priority come from the
// Simulation). init() must already have been called. Returns 0 or -errno.
int start(sim::Simulation *const *sims, size_t count);

// Read-only snapshot of task i (false if i is out of range). For monitoring/publishing.
bool get_task(size_t i, const char *&name, uint32_t &period_ms, Stats &out);

// Log per-task period, load and jitter.
void log_stats();

} // namespace rt
