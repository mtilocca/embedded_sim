#include "core/simulation.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sim_sensors, LOG_LEVEL_INF);

namespace sim {
namespace {

class Sensors : public Simulation {
public:
	const char *name() const override { return "sensors"; }
	int init() override { return 0; }
	void step(uint32_t now_ms) override { (void)now_ms; }
};

} // namespace

Simulation &sensors()
{
	static Sensors instance;
	return instance;
}

} // namespace sim
