#include "core/simulation.h"
#include "net/ws/ws.h"

namespace sim {
namespace {

// Hosts the device-side web UI: static frontend + live WebSocket (net/ws).
class WebServer : public Simulation {
public:
	const char *name() const override { return "webserver"; }
	int init() override { return net::ws::start(); }
	void step(const rt::Tick &) override {}
	uint32_t period_ms() const override { return 1000; }
};

} // namespace

Simulation &webserver()
{
	static WebServer instance;
	return instance;
}

} // namespace sim
