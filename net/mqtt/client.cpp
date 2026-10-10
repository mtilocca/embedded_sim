#include "net/mqtt/client.h"
#include "net/mqtt/topic.h"
#include "net/telemetry.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <cstring>

LOG_MODULE_REGISTER(mqtt, LOG_LEVEL_INF);

namespace net::mqtt {
namespace {

K_MSGQ_DEFINE(queue, sizeof(telemetry::Message), CONFIG_SIM_MQTT_QUEUE_LEN, 4);
K_THREAD_STACK_DEFINE(stack, 4096);
struct k_thread thread;

struct mqtt_client client;
struct sockaddr_storage broker;
uint8_t rx_buf[512];
uint8_t tx_buf[512];
char status_topic[64];
char will_payload[96];
struct mqtt_topic will_topic;
struct mqtt_utf8 will_msg;

atomic_t is_connected = ATOMIC_INIT(0);
atomic_t drops = ATOMIC_INIT(0);

void evt_cb(struct mqtt_client *, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result == 0) {
			atomic_set(&is_connected, 1);
		} else {
			LOG_ERR("CONNACK refused: %d", evt->result);
		}
		break;
	case MQTT_EVT_DISCONNECT:
		atomic_set(&is_connected, 0);
		break;
	default:
		break;
	}
}

int do_publish(const char *topic, const char *payload, size_t len, bool retain)
{
	struct mqtt_publish_param p = {};
	p.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	p.message.topic.topic.utf8 = reinterpret_cast<const uint8_t *>(topic);
	p.message.topic.topic.size = strlen(topic);
	p.message.payload.data = reinterpret_cast<uint8_t *>(const_cast<char *>(payload));
	p.message.payload.len = len;
	p.retain_flag = retain;
	return mqtt_publish(&client, &p);
}

int connect_broker()
{
	auto *a = reinterpret_cast<struct sockaddr_in *>(&broker);
	memset(&broker, 0, sizeof(broker));
	a->sin_family = AF_INET;
	a->sin_port = htons(CONFIG_SIM_MQTT_PORT);
	if (zsock_inet_pton(AF_INET, CONFIG_SIM_MQTT_BROKER, &a->sin_addr) != 1) {
		LOG_ERR("bad broker address '%s'", CONFIG_SIM_MQTT_BROKER);
		return -EINVAL;
	}

	static struct mqtt_utf8 user, pass;
	mqtt_client_init(&client);
	client.broker = &broker;
	client.evt_cb = evt_cb;
	client.client_id.utf8 = reinterpret_cast<const uint8_t *>(CONFIG_SIM_DEVICE_ID);
	client.client_id.size = strlen(CONFIG_SIM_DEVICE_ID);
	if (CONFIG_SIM_MQTT_USER[0]) {
		user.utf8 = reinterpret_cast<const uint8_t *>(CONFIG_SIM_MQTT_USER);
		user.size = strlen(CONFIG_SIM_MQTT_USER);
		pass.utf8 = reinterpret_cast<const uint8_t *>(CONFIG_SIM_MQTT_PASSWORD);
		pass.size = strlen(CONFIG_SIM_MQTT_PASSWORD);
		client.user_name = &user;
		client.password = &pass;
	}
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.keepalive = CONFIG_SIM_MQTT_KEEPALIVE_S;
	client.clean_session = 1;
	client.rx_buf = rx_buf;
	client.rx_buf_size = sizeof(rx_buf);
	client.tx_buf = tx_buf;
	client.tx_buf_size = sizeof(tx_buf);
	client.transport.type = MQTT_TRANSPORT_NON_SECURE; // TLS arrives with net/tls

	topic::status(status_topic, sizeof(status_topic));
	format_status(will_payload, sizeof(will_payload), false);
	will_topic.topic.utf8 = reinterpret_cast<const uint8_t *>(status_topic);
	will_topic.topic.size = strlen(status_topic);
	will_topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	will_msg.utf8 = reinterpret_cast<const uint8_t *>(will_payload);
	will_msg.size = strlen(will_payload);
	client.will_topic = &will_topic;
	client.will_message = &will_msg;
	client.will_retain = 1;

	return mqtt_connect(&client);
}

// Wait up to timeout_ms for the socket to become readable, then process input.
int pump(int timeout_ms)
{
	struct zsock_pollfd fds = {client.transport.tcp.sock, ZSOCK_POLLIN, 0};
	int r = zsock_poll(&fds, 1, timeout_ms);
	if (r < 0) {
		return -errno;
	}
	if (r > 0) {
		if (fds.revents & (ZSOCK_POLLHUP | ZSOCK_POLLERR | ZSOCK_POLLNVAL)) {
			return -ECONNRESET;
		}
		int ret = mqtt_input(&client);
		if (ret && ret != -EAGAIN) {
			return ret;
		}
	}
	return 0;
}

int offer(const telemetry::Message &m)
{
	if (k_msgq_put(&queue, &m, K_NO_WAIT) != 0) {
		atomic_inc(&drops);
		return -ENOMEM;
	}
	return 0;
}

void run(void *, void *, void *)
{
	for (;;) {
		atomic_set(&is_connected, 0);
		int ret = connect_broker();
		if (ret) {
			LOG_WRN("connect %s:%d failed: %d", CONFIG_SIM_MQTT_BROKER,
				CONFIG_SIM_MQTT_PORT, ret);
			k_sleep(K_SECONDS(5));
			continue;
		}

		// wait for CONNACK (up to 5 s)
		for (int i = 0; i < 50 && !atomic_get(&is_connected) && ret == 0; i++) {
			ret = pump(100);
		}
		if (ret || !atomic_get(&is_connected)) {
			LOG_WRN("no CONNACK (%d)", ret);
			mqtt_abort(&client);
			k_sleep(K_SECONDS(5));
			continue;
		}
		LOG_INF("connected to %s:%d as %s", CONFIG_SIM_MQTT_BROKER, CONFIG_SIM_MQTT_PORT,
			CONFIG_SIM_DEVICE_ID);

		char online[96];
		int n = format_status(online, sizeof(online), true);
		do_publish(status_topic, online, n, true);

		int64_t last_live = k_uptime_get();
		while (ret == 0 && atomic_get(&is_connected)) {
			telemetry::Message m;
			while (ret == 0 && k_msgq_get(&queue, &m, K_NO_WAIT) == 0) {
				char topic_s[96], body[128];
				topic::telemetry(topic_s, sizeof(topic_s), m.sim, m.signal);
				int len = format_json(body, sizeof(body), m.view());
				if (len > 0) {
					ret = do_publish(topic_s, body, len, false);
				}
			}
			if (ret == 0) {
				ret = pump(20);
			}
			if (ret == 0 && k_uptime_get() - last_live >
						CONFIG_SIM_MQTT_KEEPALIVE_S * 500) {
				ret = mqtt_live(&client);
				if (ret == -EAGAIN) {
					ret = 0; // not time to ping yet: not an error
				}
				last_live = k_uptime_get();
			}
		}
		LOG_WRN("connection lost (%d), reconnecting", ret);
		mqtt_abort(&client);
		k_sleep(K_SECONDS(2));
	}
}

} // namespace

int start()
{
	telemetry::add_sink(offer);
	k_tid_t tid = k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack), run, nullptr,
				      nullptr, nullptr, K_PRIO_PREEMPT(9), 0, K_NO_WAIT);
	k_thread_name_set(tid, "mqtt");
	return 0;
}

bool connected()
{
	return atomic_get(&is_connected) != 0;
}

uint32_t dropped()
{
	return atomic_get(&drops);
}

} // namespace net::mqtt
