#ifndef _NET_TABLE__H_
#define _NET_TABLE__H_

#include "tasvir.h"

struct Link {
	uint32_t to;
	uint32_t cost;
};

Link *create_forwarding_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us, int switch_cnt);

const Link *attach_forwarding_table(tasvir_area_desc *root_desc, int id);

Link *create_adjacency_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us, int switch_cnt);

const Link *attach_adjacency_table(tasvir_area_desc *root_desc, int id);

#endif
