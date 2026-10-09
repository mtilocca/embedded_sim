#pragma once
#include "core/simulation.h"
#include <cstddef>

namespace sim {

// Simulations enabled by Kconfig, in registration order.
Simulation *const *enabled(size_t &count);

} // namespace sim
