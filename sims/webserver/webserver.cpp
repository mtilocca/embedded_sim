#include "core/simulation.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <cstdio>

LOG_MODULE_REGISTER(sim_webserver, LOG_LEVEL_INF);

namespace sim {
namespace {

K_THREAD_STACK_DEFINE(stack, 4096);
struct k_thread thread;

// One request at a time; replies with board name and uptime as plain text.
void serve(void *, void *, void *)
{
	int srv = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_port = htons(CONFIG_SIM_WEBSERVER_PORT);
	if (srv < 0 || zsock_bind(srv, reinterpret_cast<sockaddr *>(&a), sizeof(a)) ||
	    zsock_listen(srv, 2)) {
		LOG_ERR("listen failed: %d", errno);
		return;
	}
	LOG_INF("listening on :%d", CONFIG_SIM_WEBSERVER_PORT);

	for (;;) {
		int c = zsock_accept(srv, nullptr, nullptr);
		if (c < 0) {
			continue;
		}
		char req[128];
		zsock_recv(c, req, sizeof(req), 0); // request content ignored

		char body[96], resp[192];
		int bl = snprintf(body, sizeof(body), "embedded_sim on %s, uptime %u ms\n",
				  CONFIG_BOARD, k_uptime_get_32());
		int rl = snprintf(resp, sizeof(resp),
				  "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n"
				  "Content-Length: %d\r\n\r\n%s", bl, body);
		zsock_send(c, resp, rl, 0);
		zsock_close(c);
	}
}

class WebServer : public Simulation {
public:
	const char *name() const override { return "webserver"; }
	int init() override
	{
		k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack), serve, nullptr,
				nullptr, nullptr, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
		return 0;
	}
	void step(uint32_t) override {}
};

} // namespace

Simulation &webserver()
{
	static WebServer instance;
	return instance;
}

} // namespace sim
