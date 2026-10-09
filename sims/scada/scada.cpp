#include "core/simulation.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sim_scada, LOG_LEVEL_INF);

namespace sim {
namespace {

class Scada : public Simulation {
public:
	const char *name() const override { return "scada"; }
	int init() override { return 0; }
	void step(uint32_t now_ms) override { (void)now_ms; }
};

} // namespace

Simulation &scada()
{
	static Scada instance;
	return instance;
}

} // namespace sim
