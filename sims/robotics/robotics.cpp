#include "core/simulation.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sim_robotics, LOG_LEVEL_INF);

namespace sim {
namespace {

class Robotics : public Simulation {
public:
	const char *name() const override { return "robotics"; }
	int init() override { return 0; }
	void step(uint32_t now_ms) override { (void)now_ms; }
};

} // namespace

Simulation &robotics()
{
	static Robotics instance;
	return instance;
}

} // namespace sim
