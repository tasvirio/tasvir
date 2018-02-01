#include "monitor.h"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_tcp.h>

#include "asset.h"
#include "connection.h"
#include "network_stat.h"
#include "time.h"

static inline uint32_t fast_random() {
	static uint64_t seed = 42;
	seed = seed * 1103515245 + 12345;
	return seed >> 32;
}

static inline void update_os(Asset *asset) {
	uint8_t os_id = fast_random() % PRADS_OS_CNT;
	OsAsset *os =  &asset->oasset[os_id];
	if (!os->detected_count) {
		os->sigidx = os_id;
		os->first_seen = get_epoch_time();
	}

	os->last_seen = get_epoch_time();
	os->detected_count++;
}

static inline void update_service(Asset *asset) {
	uint8_t service_id = fast_random() % PRADS_SERVICE_CNT;

	ServiceAsset *service =  &asset->sasset[service_id];
	if (!service->detected_count) {
		service->sigidx = service_id;
		service->first_seen = get_epoch_time();
		service->port = fast_random() % 65536;
		service->proto = IPPROTO_TCP;
	}

	service->last_seen = get_epoch_time();
	service->detected_count++;
}

static inline void update_asset(struct ipv4_hdr *iph, 
		AssetTable *asset_table, uint32_t random) {
	Asset *sasset, *dasset;
	auto src_entry = asset_table->Find(iph->src_addr);
	if (!src_entry) {
		Asset asset = {0};
		asset.ip_addr = iph->src_addr;
		asset.first_seen = get_epoch_time();

		src_entry = asset_table->Insert(iph->src_addr, asset);
	}

	sasset = &src_entry->second;
	sasset->last_seen = get_epoch_time();
	sasset->detected_count++;

	auto dst_entry = asset_table->Find(iph->dst_addr);
	if (!dst_entry) {
		Asset asset = {0};
		asset.ip_addr = iph->dst_addr;
		asset.first_seen = get_epoch_time();

		dst_entry = asset_table->Insert(iph->dst_addr, asset);
	}

	dasset = &dst_entry->second;
	dasset->last_seen = get_epoch_time();
	dasset->detected_count++;

	update_os(sasset);
	update_service(sasset);
	update_os(dasset);
	update_service(dasset);

	if ((random % 10000) == 0) {
		asset_table->Remove(iph->src_addr);
		asset_table->Remove(iph->dst_addr);
	}
}

static inline void update_connection(struct ipv4_hdr *iph, uint32_t len,
		ConnectionTable *conn_table, uint32_t random) {
	struct tcp_hdr *tcph = (struct tcp_hdr *)
		((char *)iph + ((iph->version_ihl & 0xf) << 2));

	// connection tracking
	bool from_server;
	FlowId fid = {0};
	if (iph->src_addr < iph->dst_addr) {
		from_server = true;
		fid.src_ip = iph->src_addr;
		fid.dst_ip = iph->dst_addr;
		fid.src_port = tcph->src_port;
		fid.dst_port = tcph->dst_port;
	} else {
		from_server = false;
		fid.src_ip = iph->src_addr;
		fid.dst_ip = iph->dst_addr;
		fid.src_port = tcph->src_port;
		fid.dst_port = tcph->dst_port;
	}

	Connection *conn = nullptr;
	auto entry = conn_table->Find(fid);
	if (!entry) {
		entry = conn_table->Insert(fid, {0});
		conn = &entry->second;

		if (from_server) {
			conn->s_ip = iph->src_addr;
			conn->d_ip = iph->dst_addr;
			conn->s_port = tcph->src_port;
			conn->d_port = tcph->dst_port;
		} else {
			conn->s_ip = iph->dst_addr;
			conn->d_ip = iph->src_addr;
			conn->s_port = tcph->dst_port;
			conn->d_port = tcph->src_port;
		}
		conn->start_time = get_epoch_time();

	} else {
		conn = &entry->second;
	}

	conn->last_pkt_time = get_epoch_time();
	if (from_server) {
		conn->s_total_pkts++;
		conn->s_total_bytes += len;
	} else {
		conn->d_total_pkts++;
		conn->d_total_bytes += len;
	}

	if ((random % 100) == 0)
		conn_table->Remove(fid);
}

static inline void monitor_tcp(struct ipv4_hdr *iph, uint32_t len,
	struct monitor_results *local_results) {

	uint32_t random = fast_random();

	update_connection(iph, len, local_results->conn_table, random);

	// source/destination ip asset tracking
	update_asset(iph, local_results->asset_table, random);
}

void monitor_packet(struct rte_mbuf *mbuf, 
		struct monitor_results *local_results) {

	struct network_stat *stat = local_results->stat;

	stat->got_packets++;

	struct ether_hdr *eth = (struct ether_hdr *)rte_pktmbuf_mtod(
			mbuf, struct ether_hdr *);
	stat->eth_recv++;

	if (eth->ether_type != rte_cpu_to_be_16(ETHER_TYPE_IPv4)) {
		return;
	}

	struct ipv4_hdr *iph = (struct ipv4_hdr *)rte_pktmbuf_mtod_offset(
			mbuf, struct ipv4_hdr *, sizeof(struct ether_hdr));
	stat->ip4_recv++;

	switch(iph->next_proto_id) {
	case IPPROTO_UDP:
		stat->udp_recv++;
		break;
	case IPPROTO_TCP:
		stat->tcp_recv++;
		monitor_tcp(iph, mbuf->buf_len, local_results);
		break;
	default:
		break;
	}
}
