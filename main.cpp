#include "core/registry.h"
#include "net/wifi.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main()
{
	LOG_INF("embedded_sim on %s", CONFIG_BOARD);

#ifdef CONFIG_SIM_WIFI
	if (net::wifi_connect(K_SECONDS(30)) != 0) {
		LOG_WRN("no network; continuing offline");
	}
#endif

	size_t n;
	auto *sims = sim::enabled(n);
	for (size_t i = 0; i < n; i++) {
		int ret = sims[i]->init();
		LOG_INF("init %s: %d", sims[i]->name(), ret);
	}

	for (;;) {
		uint32_t now = k_uptime_get_32();
		for (size_t i = 0; i < n; i++) {
			sims[i]->step(now);
		}
		k_msleep(CONFIG_SIM_TICK_MS);
	}
}
