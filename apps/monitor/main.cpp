#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "asset.h"
#include "connection.h"
#include "histogram.h"
#include "network_stat.h"
#include "monitor.h"
#include "packet_generator.h"
#include "tasvir_wrapper.h"
#include "time.h"

#define PKT_UPDATE_CNT (1000L * 1000 * 1000) // 1G Packets
#define MAX_MONITOR_CNT (24)

using namespace std;

struct thread_args {
	int id;
	int core;
	int monitor_cnt;
	uint64_t sync_int_us;
	uint64_t sync_ext_us;
};

static inline uint32_t fast_random() {
	static uint64_t seed = 42;
	seed = seed * 1103515245 + 12345;
	return seed >> 32;
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

	// initialize tasvir
	tasvir_area_desc *root_desc = tasvir_init(TASVIR_THREAD_TYPE_APP, args->core, NULL);
	if (!root_desc) {
		fprintf(stderr, "Controller: tasvir_init failed\n");
		return NULL;
	}

	// waiting all process attached to daemon
	uint64_t time_us = gettime_us();
	while (gettime_us() - time_us < 2 * 1000 * 1000)
		tasvir_service();

	// allocate memzone for global stats
	tasvir_area_desc *global_stat = allocate_global_stats(root_desc,
			args->sync_int_us, args->sync_ext_us);
	if (!global_stat) {
		fprintf(stderr, "Fail to create statistics\n");
		exit(-1);
	}
	struct network_stat *stat = (struct network_stat *)tasvir_data(global_stat);

	tasvir_service();

	tasvir_area_desc *global_asset_desc = allocate_global_asset_table(root_desc,
			args->sync_int_us, args->sync_ext_us);
	if (!global_asset_desc) {
		fprintf(stderr, "Fail to create asset table\n");
		exit(-1);
	}
	AssetTable *global_asset_table = (AssetTable *)tasvir_data(global_asset_desc);

	tasvir_service();

	tasvir_area_desc *global_connection_desc = allocate_global_connection_table(root_desc,
			args->sync_int_us, args->sync_ext_us);
	if (!global_connection_desc) {
		fprintf(stderr, "Fail to create connection table\n");
		exit(-1);
	}
	ConnectionTable *global_connection_table = (ConnectionTable *)tasvir_data(global_connection_desc);

	// waiting all process attached to daemon
	time_us = gettime_us();
	while (gettime_us() - time_us < 10 * 1000 * 1000) {
		tasvir_service();
	}

	time_us = gettime_us();
	printf("statistics %lu\n", sizeof(network_stat));
	printf("asset %lu\n", sizeof(Asset));
	printf("connection %lu\n", sizeof(Connection));

	// get local stats pointers
	const struct network_stat *local_stat[MAX_MONITOR_CNT];
	const AssetTable *asset_table[MAX_MONITOR_CNT];
	const ConnectionTable *conn_table[MAX_MONITOR_CNT];

	for (int i = 1; i <= args->monitor_cnt; i++) {
		local_stat[i] = attach_local_stats(root_desc, i);
		if (!local_stat[i]) {
			fprintf(stderr, "Fail to attach local stats from monitor %d\n", i);
			exit(-1);
		}

		asset_table[i] = attach_local_asset_table(root_desc, i);
		if (!asset_table[i]) {
			fprintf(stderr, "Fail to attach local asset stats from monitor %d\n", i);
			exit(-1);
		}

		conn_table[i] = attach_local_connection_table(root_desc, i);
		if (!conn_table[i]) {
			fprintf(stderr, "Fail to attach local connection stats from monitor %d\n", i);
			exit(-1);
		}
	}

	// read stats every 1s
	while (1) {
		tasvir_service_wrapper();
		if (gettime_us() - time_us > 1 * 1000 * 1000)  {
			time_us = gettime_us();

			uint64_t t_0, t_1, t_2, t_3;
			t_0 = gettime_us();

			// read from monitor 'i' and update
			struct network_stat temp = {};
			for (int i = 1; i <= args->monitor_cnt; i++) {
				temp.got_packets += local_stat[i]->got_packets;
			}

			// to prevent inconsistency, update at once
			memcpy(stat, &temp, sizeof(struct network_stat));
			tasvir_log_write_wrapper(stat, sizeof(struct network_stat));

			t_1 = gettime_us();

			for (int i = 1; i <= args->monitor_cnt; i++) {
				const AssetTable *local = asset_table[i];
				for (auto it = local->begin(); it != local->end(); it++) {
					auto *global = global_asset_table->Find(it->first);
					if (global) {
						aggregate_asset(&global->second, &it->second);
					} else {
						global_asset_table->Insert(it->first, it->second);
					}
				}
			}

			t_2 = gettime_us();

			for (int i = 1; i <= args->monitor_cnt; i++) {
				const ConnectionTable *local = conn_table[i];
				for (auto it = local->begin(); it != local->end(); it++) {
					auto *global = global_connection_table->Find(it->first);
					if (global) {
						aggregate_connection(&global->second, &it->second);
					} else {
						global_connection_table->Insert(it->first, it->second);
					}
				}
			}

			t_3 = gettime_us();
			printf("Aggregated #pkts : %u merging time "
					"(stat: %lu us / asset: %lu us / conn: %lu us)\n",
					stat->got_packets,
					t_1 - t_0,
					t_2 - t_1,
					t_3 - t_2);

			// lookup an random ip asset
			uint32_t rand_src_ip =
				rte_cpu_to_be_32(IPv4(192, 168, 0, fast_random() % 255));
			auto *entry = global_asset_table->Find(rand_src_ip);
			if (entry) {
				printf("Detect asset %i.%i.%i.%i (%u times), "
					"first_seen: %0.3f, last_seen %0.3f\n",
					rand_src_ip & 0xFF,
					(rand_src_ip >> 8) & 0xFF,
					(rand_src_ip >> 16) & 0xFF,
					(rand_src_ip >> 24) & 0xFF,
					entry->second.detected_count,
					entry->second.first_seen,
					entry->second.last_seen);
			}

		}
	}

	return NULL;
}

static void *run_monitor(void *thread_param) {
	struct thread_args *args = (struct thread_args *)thread_param;
	printf("running monitor %d\n", args->id);

	set_core_affinity(args->core);

	// initialize tasvir
	tasvir_area_desc *root_desc = tasvir_init(TASVIR_THREAD_TYPE_APP, args->core, NULL);
	if (!root_desc) {
		fprintf(stderr, "Monitor_%d: tasvir_init failed\n", args->id);
		return NULL;
	}

	// waiting all process attached to daemon
	uint64_t time_us = gettime_us();
	while (gettime_us() - time_us < 2 * 1000 * 1000)
		tasvir_service();

	struct monitor_results local_results;

	// allocate memzone for local stats
	tasvir_area_desc *local_desc = allocate_local_stats(root_desc, args->id,
			args->sync_int_us, args->sync_ext_us);
	if (!local_desc) {
		fprintf(stderr, "Fail to create stat\n");
		exit(-1);
	}
	local_results.stat = (struct network_stat *)tasvir_data(local_desc);

	tasvir_service();

	tasvir_area_desc *local_asset_table = allocate_local_asset_table(root_desc, args->id,
			args->sync_int_us, args->sync_ext_us);
	if (!local_asset_table) {
		fprintf(stderr, "Fail to create asset table\n");
		exit(-1);
	}
	local_results.asset_table = (AssetTable *)tasvir_data(local_asset_table);

	tasvir_service();

	tasvir_area_desc *local_connection_table = allocate_local_connection_table(root_desc, args->id,
			args->sync_int_us, args->sync_ext_us);
	if (!local_connection_table) {
		fprintf(stderr, "Fail to create connection table\n");
		exit(-1);
	}
	local_results.conn_table = (ConnectionTable *)tasvir_data(local_connection_table);

	// To measure packet latency, should be adjust as experiment set up changed
	Histogram<uint64_t> latency_histogram(
			1000	/* 1 ns * 1000 buckets --> 1 ms */,
			1		/* 1 ns */);
	const std::vector<double> latency_percentiles = {0.01, 0.5, 0.99};

	uint64_t pkt_count = 0;
	time_us = gettime_us();

	PacketGenerator packetgen;
	packetgen.generate_tcp_packets();

	// waiting all process attached to daemon
	while (gettime_us() - time_us < 5 * 1000 * 1000)
		tasvir_service();

	// running forever
	while (1) {
		rte_mbuf *pkt_pool[32];
		int ret = packetgen.receive_packets(pkt_pool, 32);
		pkt_count += ret;

		get_epoch_time(true);

		for (int i = 0; i < ret; i++) {
			// Every batch, for the first packet, we
			// 1) measure sampling packet latency, and
			// 2) do logging and synchronization
			if (i == 0) {
				uint64_t pkt_before = gettime_ns();
				// update local statistics, currently no packet I/O, just fake updating
				monitor_packet(pkt_pool[i], &local_results);
				latency_histogram.Insert(gettime_ns() - pkt_before);

				tasvir_log_write_wrapper(local_results.stat, sizeof(struct network_stat));
				tasvir_service_wrapper();
			} else {
				// update local statistics, currently no packet I/O, just fake updating
				monitor_packet(pkt_pool[i], &local_results);
			}
#define PKTS_PER_STAT 10000000L
			if (pkt_count >= PKTS_PER_STAT) {
				uint64_t diff_us = gettime_us() - time_us;
				const auto latency_summary = latency_histogram.Summarize(latency_percentiles);

				printf("Monitor_%d: %.3f Mpps\t",
						args->id,
						PKTS_PER_STAT / (double)diff_us);

				for (int i = 0; i < latency_percentiles.size(); i++) {
					printf("%.2f: %u\t",
							latency_percentiles[i],
							latency_summary.percentile_values[i]);
				}

				printf("\n");
				time_us = gettime_us();

				latency_histogram.Reset();
				pkt_count = 0;
			}
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

	struct thread_args targs = {};
	targs.monitor_cnt = nr_thread - 1;
	targs.id = tid;
	targs.core = core;
	targs.sync_int_us = sync_int_us;
	targs.sync_ext_us = sync_ext_us;

	if (tid == 0) {
		run_controller(&targs);
	} else {
		run_monitor(&targs);
	}

	return 0;
}
