#ifndef _CONNECTION__H_
#define _CONNECTION__H_
#pragma once

#include <stdint.h>

#define TASVIR
#include "cuckoo_map_lock.h"
#include "flowid.h"
#include "tasvir.h"

#define MIN(A,B) ((A<B)?(A):(B))
#define MAX(A,B) ((A>B)?(A):(B))

/*
 * Data structures are borrowed from PRADS
 */

struct Connection {
	uint32_t s_ip;          /* source address */
	uint32_t d_ip;          /* destination address */
	uint16_t s_port;        /* source port */
	uint16_t d_port;        /* destination port */
	double start_time;      /* connection start time */
	double last_pkt_time;   /* last seen packet time */
	uint64_t s_total_pkts;  /* total source packets */
	uint64_t s_total_bytes; /* total source bytes */
	uint64_t d_total_pkts;  /* total destination packets */
	uint64_t d_total_bytes; /* total destination bytes */
};

typedef CuckooMap<FlowId, Connection, Hash, EqualTo> ConnectionTable;

tasvir_area_desc *allocate_global_connection_table(tasvir_area_desc *root_desc,
		uint64_t int_us, uint64_t ext_us);
const ConnectionTable *attach_global_connection_table(tasvir_area_desc *root_desc);
tasvir_area_desc *allocate_local_connection_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us);
const ConnectionTable *attach_local_connection_table(tasvir_area_desc *root_desc, int id);

static void aggregate_connection(Connection *first, const Connection *second) {
	first->start_time = MIN(first->start_time, second->start_time);
	first->last_pkt_time = MAX(first->last_pkt_time, second->last_pkt_time);

	first->s_total_pkts += second->s_total_pkts;
	first->s_total_bytes += second->s_total_bytes;
	first->d_total_pkts += second->d_total_pkts;
	first->d_total_bytes += second->d_total_bytes;
}

#endif // #ifndef _CONNECTION__H_
