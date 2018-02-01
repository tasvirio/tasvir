#ifndef _PACKET_GEN__H_
#define _PACKET_GEN__H_

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_tcp.h>

#define PACKET_COUNT 131072
#define PACKET_SIZE 1514

class PacketGenerator {
	private:
		struct rte_mbuf *pkts[PACKET_COUNT] = {0};
		
	public:
		void generate_tcp_packets();
		int receive_packets(rte_mbuf **pkt_pool, size_t batch_size);
};

#endif
