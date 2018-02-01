#include <fcntl.h>
#include <pthread.h>
#include <math.h>
#include <rte_cycles.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "flow_table.h"
#include "histogram.h"
#include "shm_spin_lock.h"

#define PKT_UPDATE_CNT (1000L * 1000 * 1000) // 1G Packets
#define MAX_SWITCH_CNT (24)

//#define EVAL_SKEW 1

uint64_t get_epoch_time() {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return tv.tv_sec * (uint64_t)1000000 + tv.tv_usec;
}

using namespace std;

struct thread_args {
	int id;
	int core;
	int switch_cnt;
	uint64_t sync_int_us;
	uint64_t sync_ext_us;
};

static inline uint32_t fast_random() {
	static uint64_t seed = 42;
	seed = seed * 1103515245 + 12345;
	return seed >> 32;
}

static inline double fast_random_real_nonzero() {
	union {
		uint64_t i;
		double d;
	} tmp;

	static uint64_t seed = 42;
	seed = seed * 1103515245 + 12345;
	tmp.i = (seed >> 12) | 0x3ff0000000000000ul;
	return 2.0 - tmp.d;
}

// set core affinity
static void set_core_affinity(int core) {
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(core, &set);

	pthread_t tid = pthread_self();

	int ret = pthread_setaffinity_np(tid, sizeof(set), &set);
	if (ret != 0) {
		fprintf(stderr, "Fail to set affinity to core %d with error %d\n", core, ret);
	}

	return;
}

static void *run_controller(void *thread_param) {
	struct thread_args *args = (struct thread_args *)thread_param;
	printf("running controller\n");

	set_core_affinity(args->core);

	OpenFlowTable *flowtable[MAX_SWITCH_CNT];

#ifdef TASVIR
	// initialize tasvir
	tasvir_area_desc *root_desc = tasvir_init(TASVIR_THREAD_TYPE_APP, args->core, NULL);
	if (!root_desc) {
		fprintf(stderr, "Controller: tasvir_init failed\n");
		return NULL;
	}

	// create a flow table per switch
	for (int i = 1; i <= args->switch_cnt; i++) {
		flowtable[i] = create_flow_table(root_desc, i, args->sync_int_us, args->sync_ext_us);
		tasvir_service();
		printf("create_flow_table %d\n", i);
	}
#else
	// create a flow table per switch
	for (int i = 1; i <= args->switch_cnt; i++) {
		flowtable[i] = create_flow_table(i);
		if (!flowtable[i]) {
			fprintf(stderr, "Fail to create flow table %d\n", i);
			exit(-1);
		}
		printf("create_flow_table %d\n", i);
	}

	// create locks per switch
	shm_spin_lock_t* lock[MAX_SWITCH_CNT];
	for (int i = 1; i <= args->switch_cnt; i++) {
		char lockname[32];
		snprintf(lockname, sizeof(lockname), "/tmp/lock_%02d", i);
#if BIG_LOCK
		lock[i] = shm_spin_lock_create(lockname);
#else
		lock[i] = shm_spin_lock_array_create(lockname, MAX_BUCKETS);
#endif // BIG_LOCK
		if (!lock[i]) {
			fprintf(stderr, "Fail to create lock '%s'\n", lockname);
			exit(-1);
		}
		printf("create locks for switch %d\n", i);
	}
#endif // TASVIR

	uint64_t time_before = get_epoch_time();
	FlowId flowid = {0};
	uint64_t count = 0;

	int stat_count = 0;
	while (1) {
		count++;

		// keep updating flow table
		// total number of flows should not exceed
		// the number of maximum entries
#if EVAL_SKEW
		flowid.src_ip = pow(64, fast_random_real_nonzero());
		flowid.dst_ip = pow(1024, fast_random_real_nonzero());
#else
		// uniform
		flowid.src_ip = fast_random() % 64;
		flowid.dst_ip = fast_random() % 1024;
#endif

		uint64_t now = get_epoch_time();
#ifdef TASVIR
		for (int i = 1; i <= args->switch_cnt; i++) {
			auto entry = flowtable[i]->Find(flowid);
			if (!entry) {
				flowtable[i]->Insert(flowid, {count % 2, now});
			} else {
				flowtable[i]->Remove(flowid);
				flowtable[i]->Insert(flowid, {count % 2, now});
			}

		}
		
		if (count % 32 == 0)
			tasvir_service();
#else
		for (int i = 1; i <= args->switch_cnt; i++) {
			auto entry = flowtable[i]->FindWithLock(lock[i], flowid);
			if (!entry) {
				flowtable[i]->InsertWithLock(lock[i], flowid, {count % 2, now});
			} else {
				flowtable[i]->RemoveWithLock(lock[i], flowid);
				flowtable[i]->InsertWithLock(lock[i], flowid, {count % 2, now});
			}
		}
#endif

#define UPDATES_PER_STAT 1000000L
		if (count >= UPDATES_PER_STAT) {
#if 1
			if (++stat_count >= 20) {
				printf("Stop updating\n");
				while(1) {
#ifdef TASVIR
					tasvir_service();
#else
					sleep(1);
#endif
				}
			}
#endif
			uint64_t diff = now - time_before;
			printf("Controller: %.3f Million Update/s\n",
					UPDATES_PER_STAT  / (double) diff);

			count = 0;
			time_before = now;
		}
	}

	return NULL;
}

static void *run_switch(void *thread_param) {
	struct thread_args *args = (struct thread_args *)thread_param;
	printf("running switch %d\n", args->id);

	set_core_affinity(args->core);

	const OpenFlowTable *flowtable = NULL;
	uint64_t time_before;

#ifdef TASVIR
	// initialize tasvir
	tasvir_area_desc *root_desc = tasvir_init(TASVIR_THREAD_TYPE_APP, args->core, NULL);
	if (!root_desc) {
		fprintf(stderr, "Switch_%d: tasvir_init failed\n", args->id);
		return NULL;
	}

	// waiting all process attached to daemon
	time_before = get_epoch_time();
	while ((get_epoch_time() - time_before) < 5 * 1e6)
		tasvir_service();

	flowtable = attach_flow_table(root_desc, args->id);
	if (!flowtable) {
		fprintf(stderr, "Fail to attach flow table\n");
		exit(-1);
	}
#else
	sleep(5);

	flowtable = attach_flow_table(args->id);
	if (!flowtable) {
		fprintf(stderr, "Fail to attach flow table\n");
		exit(-1);
	}

	char lockname[32];
	snprintf(lockname, sizeof(lockname), "/tmp/lock_%02d", args->id);
#if BIG_LOCK
	shm_spin_lock_t *lock = shm_spin_lock_attach(lockname);
#else
	shm_spin_lock_t *lock = shm_spin_lock_array_attach(lockname, MAX_BUCKETS);
#endif // BIG_LOCK
	if (!lock) {
		fprintf(stderr, "Fail to get a lock '%s'\n", lockname);
		exit(-1);
	}
#endif
	printf("Switch_%d: attach flowtable\n", args->id);

	typedef struct {
		uint64_t matched;
		uint64_t not_matched;
		uint64_t allow;
		uint64_t drop;
	} lookup_stat;

	FlowId flowid = {0};
	lookup_stat stats = {0};
	int lookup_count = 0;
	time_before = get_epoch_time();

	// To measure packet latency, should be adjust as experiment set up changed
	//Histogram<uint64_t> latency_histogram(
	//		1000    /* 10 us * 1000 buckets --> 10 ms */,
	//		10       /* 10 us */);
	//const std::vector<double> latency_percentiles = {0.01, 0.5, 0.99};

	while (1) {
		lookup_count++;

#ifdef TASVIR
		// Keep synching flowtable
		tasvir_service();
#endif

#if EVAL_SKEW
		flowid.src_ip = pow(64, fast_random_real_nonzero());
		flowid.dst_ip = pow(1024, fast_random_real_nonzero());
#else
		// uniform
		flowid.src_ip = fast_random() % 64;
		flowid.dst_ip = fast_random() % 1024;
#endif

		uint64_t now = get_epoch_time();
#ifdef TASVIR
		auto entry = flowtable->Find(flowid);
#else
		auto entry = flowtable->FindWithLock(lock, flowid);
#endif

#if 1
		if (!entry) {
			stats.not_matched++;
		} else {
			//latency_histogram.Insert(now - entry->second.update_time);
			stats.matched++;
			if (entry->second.is_forward == true) {
				stats.allow++;
			} else {
				stats.drop++;
			}
		}
#endif

#define LOOKUPS_PER_STAT 1000000L
		if (lookup_count >= LOOKUPS_PER_STAT) {
			double diff = now - time_before;
			printf("Switch_%d: %.3f Million Lookup/s "
					"matched: %u ( allow: %u, drop: %u) unmatched: %u \n", 
					args->id,
					UPDATES_PER_STAT  / (double) diff,
					stats.matched, stats.allow,
					stats.drop, stats.not_matched);

			//const auto latency_summary =
			//	latency_histogram.Summarize(latency_percentiles);

			//for (int i = 0; i < latency_percentiles.size(); i++) {
			//	printf("%.2f: %u\t",
			//			latency_percentiles[i],
			//			latency_summary.percentile_values[i]);
			//}

			//printf("\n");

			//latency_histogram.Reset();
			lookup_count = 0;
			stats = {0};
			time_before = now;
		}
	}

	return NULL;
}

int main(int argc, char **argv) {
	if (argc < 4) {
		fprintf(stderr, "usage: %s thread_id core_id nr_thread\n", argv[0]);
		return -1;
	}

	int tid = atoi(argv[1]);
	int core = atoi(argv[2]);
	int nr_thread = atoi(argv[3]);

	uint64_t sync_int_us = 100000;
	uint64_t sync_ext_us = 100000;

	if (argc >= 5)
		sync_int_us = atol(argv[4]);
	if (argc >= 6)
		sync_ext_us = atol(argv[5]);

	printf("Run %s (tid:%d) in core %d syncrate: %.fms/%.fms\n",
			(tid==0?"Controller":"Switch"), tid, core,
			sync_int_us/1e3, sync_ext_us/1e3);

#if EVAL_SKEW
       printf("SKEWED INPUT\n");
#else
       printf("UNIFORM INPUT\n");
#endif

#ifdef TASVIR
       printf("TASVIR\n");
#else
#if BIG_LOCK
       printf("SHARED_MEMORY BIG_LOCK\n");
#else
       printf("SHARED_MEMORY SMALL_LOCK\n");
#endif
#endif

	struct thread_args targs = {};
	targs.switch_cnt = nr_thread - 1;
	targs.id = tid;
	targs.core = core;
	targs.sync_int_us = sync_int_us;
	targs.sync_ext_us = sync_ext_us;

	if (tid == 0) {
		run_controller(&targs);
	} else {
		run_switch(&targs);
	}

	return 0;
}
