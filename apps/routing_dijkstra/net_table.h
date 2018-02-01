#ifndef _NET_TABLE__H_
#define _NET_TABLE__H_

#include "tasvir.h"

#define NEIGHBOR_PER_SWITCH 3

struct Link {
	uint32_t to;
	uint32_t cost;
};

struct Forward {
	uint32_t to;
	uint32_t cost;
};

Forward *create_forwarding_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us, int switch_cnt, int arr_cnt);

const Forward *attach_forwarding_table(tasvir_area_desc *root_desc, int id);

Link *create_adjacency_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us, int arr_cnt);

const Link *attach_adjacency_table(tasvir_area_desc *root_desc, int id);

#endif
