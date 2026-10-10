#include "core/simulation.h"
#include <zephyr/logging/log.h>
#include <cmath>
#include "net/telemetry.h"

LOG_MODULE_REGISTER(sim_sensors, LOG_LEVEL_INF);

namespace sim {
namespace {

class Sensors : public Simulation {
public:
	const char *name() const override { return "sensors"; }
	int init() override { return 0; }
	void step(const rt::Tick &t) override
	{
		// Placeholder signal so the data path (MQTT -> Influx/Grafana/web) can be
		// tested end to end: 20 degC +/- 5, 60 s period, published at 2 Hz.
		if (t.seq % 5 == 0) {
			const float s = static_cast<float>(t.now_us) * 1e-6f;
			net::telemetry::publish("sensors", "demo_temp",
					   {20.0f + 5.0f * sinf(2.0f * 3.14159265f * s / 60.0f), "degC"});
		}
	}
};

} // namespace

Simulation &sensors()
{
	static Sensors instance;
	return instance;
}

} // namespace sim
