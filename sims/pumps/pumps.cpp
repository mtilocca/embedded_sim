#include "core/simulation.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sim_pumps, LOG_LEVEL_INF);

namespace sim {
namespace {

class Pumps : public Simulation {
public:
	const char *name() const override { return "pumps"; }
	int init() override { return 0; }
	void step(uint32_t now_ms) override { (void)now_ms; }
};

} // namespace

Simulation &pumps()
{
	static Pumps instance;
	return instance;
}

} // namespace sim
