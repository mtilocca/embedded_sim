#include "net/ws/ws.h"
#include "net/mqtt/topic.h"
#include "net/telemetry.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/websocket.h>
#include <cstdio>

LOG_MODULE_REGISTER(ws, LOG_LEVEL_INF);

namespace net::ws {
namespace {

K_MSGQ_DEFINE(queue, sizeof(telemetry::Message), CONFIG_SIM_WS_QUEUE_LEN, 4);
K_THREAD_STACK_DEFINE(stack, 3072);
struct k_thread thread;
K_MUTEX_DEFINE(clients_lock);

// One entry per connected viewer. `seq` orders them so the oldest can be evicted.
struct Client {
	int sock = -1;
	uint32_t seq = 0;
};
Client clients[CONFIG_SIM_WS_MAX_CLIENTS];
uint32_t next_seq;

int offer(const telemetry::Message &m)
{
	return k_msgq_put(&queue, &m, K_NO_WAIT) == 0 ? 0 : -ENOMEM;
}

// Send one text frame to every client; drop clients whose socket failed.
void broadcast(const char *buf, int len)
{
	k_mutex_lock(&clients_lock, K_FOREVER);
	for (Client &c : clients) {
		if (c.sock < 0) {
			continue;
		}
		int ret = websocket_send_msg(c.sock, reinterpret_cast<const uint8_t *>(buf), len,
					     WEBSOCKET_OPCODE_DATA_TEXT, false, true, 200);
		if (ret < 0) {
			LOG_INF("ws client dropped (%d)", ret);
			websocket_unregister(c.sock);
			c.sock = -1;
		}
	}
	k_mutex_unlock(&clients_lock);
}

bool have_clients()
{
	bool any = false;
	k_mutex_lock(&clients_lock, K_FOREVER);
	for (const Client &c : clients) {
		any = any || c.sock >= 0;
	}
	k_mutex_unlock(&clients_lock);
	return any;
}

// Frames use the same shape as an MQTT message so the frontend treats both
// sources identically: {"topic":"embedded_sim/<dev>/<sim>/<sig>","p":{...}}
void run(void *, void *, void *)
{
	int64_t next_hb = k_uptime_get();
	for (;;) {
		int64_t wait = next_hb - k_uptime_get();
		telemetry::Message m;
		char topic_s[96], body[128], frame[256];

		if (k_msgq_get(&queue, &m, wait > 0 ? K_MSEC(wait) : K_NO_WAIT) == 0) {
			if (have_clients()) {
				mqtt::topic::telemetry(topic_s, sizeof(topic_s), m.sim, m.signal);
				if (mqtt::format_json(body, sizeof(body), m.view()) > 0) {
					int n = snprintf(frame, sizeof(frame),
							 "{\"topic\":\"%s\",\"p\":%s}", topic_s, body);
					if (n > 0 && n < static_cast<int>(sizeof(frame))) {
						broadcast(frame, n);
					}
				}
			}
		}

		if (k_uptime_get() >= next_hb) {
			next_hb += 1000;
			if (have_clients()) {
				mqtt::topic::status(topic_s, sizeof(topic_s));
				mqtt::format_status(body, sizeof(body), true);
				int n = snprintf(frame, sizeof(frame), "{\"topic\":\"%s\",\"p\":%s}",
						 topic_s, body);
				if (n > 0 && n < static_cast<int>(sizeof(frame))) {
					broadcast(frame, n);
				}
			}
		}
	}
}

} // namespace

int start()
{
	telemetry::add_sink(offer);
	k_tid_t tid = k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack), run, nullptr,
				      nullptr, nullptr, K_PRIO_PREEMPT(9), 0, K_NO_WAIT);
	k_thread_name_set(tid, "ws");
	int ret = http_server_start();
	LOG_INF("http server on :%d (ret %d)", CONFIG_SIM_WEBSERVER_PORT, ret);
	return ret;
}

} // namespace net::ws

extern "C" int sim_ws_setup(int ws_socket, struct http_request_ctx *, void *)
{
	using namespace net::ws;

	k_mutex_lock(&clients_lock, K_FOREVER);

	// Free slot, else evict the oldest viewer: the newest connection wins, so
	// stale tabs or phones that left the Wi-Fi cannot lock everyone else out.
	Client *slot = nullptr;
	for (Client &c : clients) {
		if (c.sock < 0) {
			slot = &c;
			break;
		}
		if (!slot || static_cast<int32_t>(c.seq - slot->seq) < 0) {
			slot = &c;
		}
	}
	if (slot->sock >= 0) {
		LOG_INF("ws table full, evicting oldest viewer");
		websocket_unregister(slot->sock);
	}
	slot->sock = ws_socket;
	slot->seq = next_seq++;

	k_mutex_unlock(&clients_lock);

	LOG_INF("ws client connected");
	return 0;
}
