#include "packet_generator.h"

#include <assert.h>
#include <math.h>

#define EVAL_SKEW 1

#define SET_LLADDR(lladdr, a, b, c, d, e, f) \
  do {                                       \
    (lladdr).addr_bytes[0] = a;              \
    (lladdr).addr_bytes[1] = b;              \
    (lladdr).addr_bytes[2] = c;              \
    (lladdr).addr_bytes[3] = d;              \
    (lladdr).addr_bytes[4] = e;              \
    (lladdr).addr_bytes[5] = f;              \
  } while (0)

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

static void build_template(char *pkt_tmp, int size) {
  struct ether_hdr *eth_tmp = (struct ether_hdr *)pkt_tmp;
  struct ipv4_hdr *ip_tmp =
      (struct ipv4_hdr *)(pkt_tmp + sizeof(struct ether_hdr));
  struct tcp_hdr *tcp_tmp =
      (struct tcp_hdr *)(pkt_tmp + sizeof(struct ether_hdr) +
                         sizeof(struct ipv4_hdr));

  SET_LLADDR(eth_tmp->d_addr, 0, 0, 0, 0, 0, 2);
  SET_LLADDR(eth_tmp->s_addr, 0, 0, 0, 0, 0, 1);
  eth_tmp->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

  ip_tmp->version_ihl = (4 << 4) | sizeof(struct ipv4_hdr) >> 2;
  ip_tmp->type_of_service = 0;
  ip_tmp->total_length = rte_cpu_to_be_16(size - sizeof(*eth_tmp));
  ip_tmp->packet_id = rte_cpu_to_be_16(0);
  ip_tmp->fragment_offset = rte_cpu_to_be_16(0);
  ip_tmp->time_to_live = 64;
  ip_tmp->next_proto_id = IPPROTO_TCP;
  ip_tmp->dst_addr = rte_cpu_to_be_32(IPv4(192, 168, 0, 2));
  ip_tmp->src_addr = rte_cpu_to_be_32(IPv4(192, 168, 0, 0));
  rte_ipv4_cksum(ip_tmp);

  tcp_tmp->src_port = rte_cpu_to_be_16(1234);
  tcp_tmp->dst_port = rte_cpu_to_be_16(5678);
  tcp_tmp->sent_seq = 0x12341234;
  tcp_tmp->recv_ack = 0x34241231;
}

void PacketGenerator::generate_tcp_packets() {
	char pkt_tmp[PACKET_SIZE];
	build_template(pkt_tmp, PACKET_SIZE);
	
	for (int i = 0; i < PACKET_COUNT; i++) {
		pkts[i] = (struct rte_mbuf *)calloc(1, 2048);
		struct rte_mbuf *mbuf = pkts[i];
		assert(mbuf);
		

		// those packets should be only used in this process!
		// just for working rte_pktmbuf_mtod() or rte_pktmbuf_dump();
		mbuf->pkt_len = mbuf->data_len = PACKET_SIZE;
		mbuf->buf_len = 2048 - sizeof(struct rte_mbuf);
		mbuf->nb_segs = 1;
		mbuf->next = NULL;
		mbuf->buf_addr = mbuf + 1;
		mbuf->data_off = RTE_PKTMBUF_HEADROOM;
		memcpy((char *)mbuf->buf_addr + mbuf->data_off, pkt_tmp, PACKET_SIZE);

		// modify packet contents if necessary
		struct ipv4_hdr *iph = rte_pktmbuf_mtod_offset(
				(struct rte_mbuf *)mbuf, struct ipv4_hdr *, sizeof(struct ether_hdr));
#if EVAL_SKEW
		iph->src_addr = IPv4(192, 168, 0, (int)pow(64, fast_random_real_nonzero()));
		iph->dst_addr = IPv4(172, 10, 4, (int)pow(1024, fast_random_real_nonzero()));
#else
		iph->src_addr = IPv4(192, 168, 0, fast_random() % 64);
		iph->dst_addr = IPv4(172, 10, 4, fast_random() % 1024);
#endif
	}
}

int PacketGenerator::receive_packets(rte_mbuf **pkt_pool, size_t batch_size) {
	static int index = 0;
	int pkt_remain = PACKET_COUNT - index;
	int ret_pkt_count = (pkt_remain >= batch_size)?batch_size:pkt_remain;

	memcpy(pkt_pool, pkts + index, sizeof(rte_mbuf *) * ret_pkt_count);

	index = (index + ret_pkt_count) % PACKET_COUNT;
	return ret_pkt_count;
}
