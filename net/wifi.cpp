#include "net/wifi.h"

#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <cstring>

LOG_MODULE_REGISTER(wifi, LOG_LEVEL_INF);

namespace net {
namespace {

K_SEM_DEFINE(connected_sem, 0, 1);
struct net_mgmt_event_callback cb;

void on_event(struct net_mgmt_event_callback *, uint64_t event, struct net_if *)
{
	if (event == NET_EVENT_L4_CONNECTED) {
		k_sem_give(&connected_sem);
	}
}

} // namespace

int wifi_connect(k_timeout_t timeout)
{
#ifdef CONFIG_SIM_WIFI
	struct net_if *iface = net_if_get_first_wifi();

	if (iface == nullptr) {
		LOG_ERR("no Wi-Fi interface");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&cb, on_event, NET_EVENT_L4_CONNECTED);
	net_mgmt_add_event_callback(&cb);

	struct wifi_connect_req_params p = {};
	p.ssid = reinterpret_cast<const uint8_t *>(CONFIG_SIM_WIFI_SSID);
	p.ssid_length = strlen(CONFIG_SIM_WIFI_SSID);
	p.psk = reinterpret_cast<const uint8_t *>(CONFIG_SIM_WIFI_PSK);
	p.psk_length = strlen(CONFIG_SIM_WIFI_PSK);
	p.security = p.psk_length ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
	p.band = WIFI_FREQ_BAND_UNKNOWN;
	p.channel = WIFI_CHANNEL_ANY;

	if (CONFIG_SIM_STATIC_IPV4[0] != '\0') {
		struct in_addr addr, mask, gw;
		net_addr_pton(AF_INET, CONFIG_SIM_STATIC_IPV4, &addr);
		net_addr_pton(AF_INET, CONFIG_SIM_NETMASK_IPV4, &mask);
		net_addr_pton(AF_INET, CONFIG_SIM_GATEWAY_IPV4, &gw);
		net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
		net_if_ipv4_set_netmask_by_addr(iface, &addr, &mask);
		net_if_ipv4_set_gw(iface, &gw);
	}

	LOG_INF("joining '%s'", CONFIG_SIM_WIFI_SSID);
	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p));
	if (ret) {
		LOG_ERR("connect request failed: %d", ret);
		return ret;
	}
	return k_sem_take(&connected_sem, timeout);
#else
	(void)timeout;
	return -ENOTSUP;
#endif
}

} // namespace net
