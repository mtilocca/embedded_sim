#include "core/rt/runner.h"
#include "core/rt/clock.h"
#include "core/rt/stats.h"
#include "core/rt/tick.h"
#include "core/simulation.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rt, LOG_LEVEL_INF);

namespace rt {
namespace {

struct Slot {
	sim::Simulation *sim = nullptr;
	uint32_t period_us = 0;
	Stats stats;
	struct k_thread thread;
};

Slot slots[CONFIG_SIM_MAX_TASKS];
size_t slot_count;
K_THREAD_STACK_ARRAY_DEFINE(stacks, CONFIG_SIM_MAX_TASKS, CONFIG_SIM_TASK_STACK_SIZE);

// Drift-free periodic loop: deadlines advance by exactly one period. If a step
// overruns, missed deadlines are skipped (counted), never queued up.
void task_main(void *p1, void *, void *)
{
	Slot &s = *static_cast<Slot *>(p1);
	const int64_t period = k_us_to_ticks_ceil64(s.period_us);
	int64_t next = k_uptime_ticks() + period;
	uint32_t seq = 0;

	for (;;) {
		k_sleep(K_TIMEOUT_ABS_TICKS(next));

		const int64_t start = k_uptime_ticks();
		const uint32_t jitter = k_ticks_to_us_floor32(start - next);

		s.sim->step(Tick{k_ticks_to_us_floor64(start), s.period_us, seq++});

		const int64_t end = k_uptime_ticks();
		const uint32_t exec = k_ticks_to_us_floor32(end - start);

		s.stats.runs++;
		s.stats.sum_exec_us += exec;
		if (exec > s.stats.max_exec_us) {
			s.stats.max_exec_us = exec;
		}
		if (jitter > s.stats.max_jitter_us) {
			s.stats.max_jitter_us = jitter;
		}

		next += period;
		if (end >= next) {
			s.stats.overruns++;
			next += period * ((end - next) / period + 1);
		}
	}
}

} // namespace

int start(sim::Simulation *const *sims, size_t count)
{
	if (count > CONFIG_SIM_MAX_TASKS) {
		return -ENOMEM;
	}
	slot_count = count;
	for (size_t i = 0; i < count; i++) {
		Slot &s = slots[i];
		s.sim = sims[i];
		s.period_us = sims[i]->period_ms() * 1000u;
		k_tid_t tid = k_thread_create(&s.thread, stacks[i], K_THREAD_STACK_SIZEOF(stacks[i]),
					      task_main, &s, nullptr, nullptr,
					      K_PRIO_PREEMPT(sims[i]->priority()), 0, K_NO_WAIT);
		k_thread_name_set(tid, sims[i]->name());
	}
	return 0;
}

bool get_task(size_t i, const char *&name, uint32_t &period_ms, Stats &out)
{
	if (i >= slot_count) {
		return false;
	}
	name = slots[i].sim->name();
	period_ms = slots[i].sim->period_ms();
	out = slots[i].stats;
	return true;
}

void log_stats()
{
	for (size_t i = 0; i < slot_count; i++) {
		const Slot &s = slots[i];
		const uint32_t runs = s.stats.runs ? s.stats.runs : 1;
		const uint32_t avg = static_cast<uint32_t>(s.stats.sum_exec_us / runs);
		LOG_INF("%-10s T=%ums runs=%u over=%u jitter_max=%uus exec_avg=%uus exec_max=%uus",
			s.sim->name(), s.sim->period_ms(), s.stats.runs, s.stats.overruns,
			s.stats.max_jitter_us, avg, s.stats.max_exec_us);
	}
}

} // namespace rt
