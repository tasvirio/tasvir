#include <fcntl.h>
#include <pthread.h>
#include <math.h>
#include <rte_cycles.h>
#include <queue>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "net_table.h"
#include "tasvir.h"

#define MAX_SWITCH_CNT (24)

using namespace std;

struct thread_args {
	int id;
	int core;
	int process_cnt;
	int subthread_cnt;
	uint64_t sync_int_us;
	uint64_t sync_ext_us;
};

uint64_t gettime_us() { return 1E6 * rte_rdtsc() / rte_get_tsc_hz(); }
uint64_t gettime_ns() { return 1E9 * rte_rdtsc() / rte_get_tsc_hz(); }

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

uint64_t get_epoch_time() {
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return tv.tv_sec * (uint64_t)1000000 + tv.tv_usec;
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
	int switch_cnt = args->process_cnt * args->subthread_cnt;

	set_core_affinity(args->core);

	// initialize tasvir
	tasvir_area_desc *root_desc = tasvir_init(TASVIR_THREAD_TYPE_APP, args->core, NULL);
	if (!root_desc) {
		fprintf(stderr, "Controller: tasvir_init failed\n");
		return NULL;
	}

	Forward *fwd_arr[switch_cnt];
	const Link *adj_arr[switch_cnt];

	for (int i = 0; i < args->process_cnt; i++) {
		Forward *f_table = create_forwarding_table(root_desc, i,
				args->sync_int_us, args->sync_ext_us,
				switch_cnt, args->subthread_cnt);
		if (!f_table) {
			fprintf(stderr, "Fail to create forwarding table\n");
			exit(-1);
		}
		printf("Switch_%d: Create forwarding table\n", i);

		tasvir_service();

		for (int j = 0; j < args->subthread_cnt; j++) {
			fwd_arr[i * args->subthread_cnt + j] = f_table + j * switch_cnt;
		}
	}


	uint64_t now = gettime_us();
	while (gettime_us() - now < 2 * 1e6) {
		tasvir_service();
	}
	now = gettime_us();

	for (int i = 0; i < args->process_cnt; i++) {
		const Link *l_table = attach_adjacency_table(root_desc, i);
		if (!l_table) {
			fprintf(stderr, "Fail to attach adjacency table\n");
			exit(-1);
		}
		printf("Switch_%d: attach adjacency table\n", i);

		tasvir_service();

		for (int j = 0; j < args->subthread_cnt; j++) {
			adj_arr[i * args->subthread_cnt + j] = l_table + j * NEIGHBOR_PER_SWITCH;
		}
	}

	while (1) {
		while (gettime_us() - now < 1 * 1e6) {
			tasvir_service();
		}
		now = gettime_us();

		/* get link-state all-pair shorted path */
		
		// 1. initialize a table
		Forward table[switch_cnt][switch_cnt];
		memset(table, 0xFFFFFFFF, sizeof(Forward)*switch_cnt*switch_cnt);
		for (int i = 0; i < switch_cnt; i++) {
			table[i][i].to = i;
			table[i][i].cost = 0;
		}

		//for (int i = 0; i < switch_cnt; i++) {
		//	const Link *adj = adj_arr[i];
		//	for (int j = 0; j < NEIGHBOR_PER_SWITCH; j++) {
		//		printf("%u -> %u : %u\n", i, adj[j].to, adj[j].cost);
		//	}
		//}

		struct NextHop {
			uint32_t from;
			uint32_t to;
			uint32_t first_hop;
			uint32_t total_cost;
		};
		struct compare {
			bool operator() (const NextHop& lhs, const NextHop& rhs) const {
				return lhs.total_cost > rhs.total_cost;
			}
		};

		for (int i = 0; i < switch_cnt; i++) {
			//printf("Running dijkstra from source %d\n", i);
			const Link *adj = adj_arr[i];
		
			std::priority_queue<NextHop, std::vector<NextHop>, compare> next;
			for (int j = 0; j < NEIGHBOR_PER_SWITCH; j++) {
				next.push({i, adj[j].to, adj[j].to, adj[j].cost});
			}
		
			while (!next.empty())
			{
				NextHop hop = next.top();
				next.pop();

				uint32_t cur = hop.to;
				if (table[i][cur].cost > hop.total_cost) {
					table[i][cur].to = hop.first_hop;
					table[i][cur].cost = hop.total_cost;

					const Link *next_adj = adj_arr[cur];
					for (int k = 0; k < NEIGHBOR_PER_SWITCH; k++) {
						next.push({hop.to, 
								next_adj[k].to, 
								hop.first_hop, 
								hop.total_cost + next_adj[k].cost});
					}
				}
			}
		}

		printf("Get a network graph: %lu us\n", (gettime_us() - now));

		// 3. publish new results
		for (int i = 0; i < switch_cnt ; i++) {
			Forward *fwd = fwd_arr[i];
			//printf("Forwarding table switch: %d\n", i);
			for (int j = 0; j < switch_cnt; j++) {
				Forward *now = fwd + j;
				now->to = table[i][j].to;
				now->cost = table[i][j].cost;
				//printf("\t %d (%u,%u)\n", j, now->to, now->cost);
				tasvir_log_write(now, sizeof(Link));
			}
		}
		
		printf("Copy to switches: %lu us\n", (gettime_us() - now));
	}

	return NULL;
}

static void *run_switch(void *thread_param) {
	struct thread_args *args = (struct thread_args *)thread_param;
	printf("running switch %d\n", args->id);
	int switch_cnt = args->process_cnt * args->subthread_cnt;
	int memzone_id = args->id - 1;

	set_core_affinity(args->core);

	// initialize tasvir
	tasvir_area_desc *root_desc = tasvir_init(TASVIR_THREAD_TYPE_APP,
			args->core, NULL);
	if (!root_desc) {
		fprintf(stderr, "Switch_%d: tasvir_init failed\n", args->id);
		return NULL;
	}

	uint64_t now = gettime_us();
	while (gettime_us() - now < 2 * 1e6) {
		tasvir_service();
	}
	now = gettime_us();

	Link *adj_arr[args->subthread_cnt];
	const Forward *fwd_arr[args->subthread_cnt];

	Link *l_table = create_adjacency_table(root_desc, memzone_id,
			args->sync_int_us, args->sync_ext_us, args->subthread_cnt);
	if (!l_table) {
		fprintf(stderr, "Fail to create adjacency table %d\n", memzone_id);
		exit(-1);
	}
	printf("Switch_%d: create adjacency table\n", memzone_id);
	tasvir_service();
	for (int n = 0; n < args->subthread_cnt; n++) {
		adj_arr[n] = l_table + n * NEIGHBOR_PER_SWITCH;
	}

	now = gettime_us();
	while (gettime_us() - now < 2 * 1e6) {
		tasvir_service();
	}
	now = gettime_us();

	const Forward *f_table = attach_forwarding_table(root_desc, memzone_id);
	if (!f_table) {
		fprintf(stderr, "Fail to attach forwarding table %d\n", memzone_id);
		exit(-1);
	}
	printf("Switch_%d: attach forwarding table\n", memzone_id);
	tasvir_service();

	for (int n = 0; n < args->subthread_cnt; n++) {
		fwd_arr[n] = f_table + n * switch_cnt;
	}

	while (1) {
		// Keep synching
		while (gettime_us() - now < 1 * 1e7) {
			tasvir_service();
		}
		now = gettime_us();
		// update link status
		for (int n = 0; n < args->subthread_cnt; n++) {
			Link *adj = adj_arr[n];
			int table_id = (args->id - 1) * (args->subthread_cnt) + n;
			//printf("Switch %d\n", table_id);
			for (int i = 0; i < NEIGHBOR_PER_SWITCH; i++) {
				Link *now = adj + i;
				do {
					now->to = fast_random() % switch_cnt;
				} while(now->to == table_id);
				now->cost = fast_random() % 10 + 1;
				
				//printf("Link state forward: %d cost: %u\n", now->to, now->cost);
				tasvir_log_write(now, sizeof(Link));
			}
		}

		// get forwarding table
		for (int n = 0; n < args->subthread_cnt; n++) {
			const Forward *fwd = fwd_arr[n];
			int table_id = (args->id - 1) * (args->subthread_cnt) + n;
			for (int i = 0; i < switch_cnt; i++) {
				const Forward *now = fwd + i;
				if (now->to != i && now->to != 0xFFFFFFFF)
					printf("%d -> %d forward: %u cost: %u\n", 
							table_id, i, now->to, now->cost);
			}
		}
	}

	return NULL;
}

int main(int argc, char **argv) {
	if (argc < 5) {
		fprintf(stderr, "usage: %s thread_id core_id nr_thread nr_subthread\n", argv[0]);
		return -1;
	}

	int tid = atoi(argv[1]);
	int core = atoi(argv[2]);
	int nr_thread = atoi(argv[3]);
	int nr_subthread = atoi(argv[4]);

	uint64_t sync_int_us = 100000;
	uint64_t sync_ext_us = 500000;

	if (argc >= 6)
		sync_int_us = atol(argv[5]);
	if (argc >= 7)
		sync_ext_us = atol(argv[6]);

	printf("Run %s (tid:%d) in core %d syncrate: %.fms/%.fms\n",
			(tid==0?"Controller":"Switch"), tid, core,
			sync_int_us/1e3, sync_ext_us/1e3);

	struct thread_args targs = {};
	targs.process_cnt = nr_thread - 1;
	targs.id = tid;
	targs.core = core;
	targs.subthread_cnt = nr_subthread;
	targs.sync_int_us = sync_int_us;
	targs.sync_ext_us = sync_ext_us;

	if (tid == 0) {
		run_controller(&targs);
	} else {
		run_switch(&targs);
	}

	return 0;
}
