#include "time.h"

#include <rte_cycles.h>
#include <sys/time.h>

uint64_t gettime_us() { return 1E6 * rte_rdtsc() / rte_get_tsc_hz(); }
uint64_t gettime_ns() { return 1E9 * rte_rdtsc() / rte_get_tsc_hz(); }

double get_epoch_time(bool set) {
	static double time = 0;
	if (set) {
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		time = tv.tv_sec + tv.tv_usec / 1e6;
	}
	return time;
}

