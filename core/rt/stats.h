#pragma once
#include <cstdint>

namespace rt {

// Per-task timing, written by the task thread, read (racy, diagnostic only) by the logger.
struct Stats {
	uint32_t runs = 0;
	uint32_t overruns = 0;      // steps that ended after their next deadline
	uint32_t max_jitter_us = 0; // worst lateness of a step start vs its deadline
	uint32_t max_exec_us = 0;   // worst step() duration
	uint64_t sum_exec_us = 0;
};

} // namespace rt
