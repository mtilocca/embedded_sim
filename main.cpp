#include "core/registry.h"
#include "core/rt/runner.h"
#include "net/wifi.h"
#include "net/telemetry.h"
#ifdef CONFIG_SIM_MQTT
#include "net/mqtt/client.h"
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <cstdio>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// Timing health of every task on <root>/<device>/rt/<task>_<metric>.
static void publish_rt_stats()
{
	const char *name;
	uint32_t period;
	rt::Stats st;
	char sig[24];
	for (size_t i = 0; rt::get_task(i, name, period, st); i++) {
		const uint32_t runs = st.runs ? st.runs : 1;
		snprintf(sig, sizeof(sig), "%s_jitter_max", name);
		net::telemetry::publish("rt", sig, {static_cast<float>(st.max_jitter_us), "us"});
		snprintf(sig, sizeof(sig), "%s_exec_avg", name);
		net::telemetry::publish("rt", sig, {static_cast<float>(st.sum_exec_us / runs), "us"});
		snprintf(sig, sizeof(sig), "%s_overruns", name);
		net::telemetry::publish("rt", sig, {static_cast<float>(st.overruns), "count"});
	}
}

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

#ifdef CONFIG_SIM_MQTT
	net::mqtt::start();
#endif

	int ret = rt::start(sims, n);
	if (ret) {
		LOG_ERR("rt start failed: %d", ret);
		return ret;
	}

	// Simulations now run on their own threads; main just reports timing health.
	for (;;) {
		k_sleep(K_SECONDS(CONFIG_SIM_STATS_PERIOD_S ? CONFIG_SIM_STATS_PERIOD_S : 3600));
		if (CONFIG_SIM_STATS_PERIOD_S) {
			rt::log_stats();
			publish_rt_stats();
		}
	}
}
