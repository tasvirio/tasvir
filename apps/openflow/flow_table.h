#ifndef _FLOW_TABLE__H_
#define _FLOW_TABLE__H_

#include "cuckoo_map_lock.h"

#define MAX_BUCKETS (256 * 1024)
#define MAX_ENTRIES (MAX_BUCKETS * 4)

struct FlowId {
	uint32_t src_ip;
	uint32_t dst_ip;
	uint16_t src_port;
	uint16_t dst_port;
	uint8_t protocol;
};

struct Policy{
	bool is_forward;
	uint64_t update_time; /* internal-machine only */
};

// hashes a FlowId
struct Hash {
	// a similar method to boost's hash_combine in order to combine hashes
	inline void combine(std::size_t& hash, const unsigned int& val) const {
		std::hash<unsigned int> hasher;
		hash ^= hasher(val) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
	}
	HashResult operator()(const FlowId& id) const {
		std::size_t hash = 0;
		combine(hash, id.src_ip);
		combine(hash, id.dst_ip);
		combine(hash, id.src_port);
		combine(hash, id.dst_port);
		combine(hash, (uint32_t)id.protocol);
		return hash;
	}
};

// to compare two FlowId for equality in a hash table
struct EqualTo {
	bool operator()(const FlowId& id1, const FlowId& id2) const {
		bool ips = (id1.src_ip == id2.src_ip) && (id1.dst_ip == id2.dst_ip);
		bool ports =
			(id1.src_port == id2.src_port) && (id1.dst_port == id2.dst_port);
		return (ips && ports) && (id1.protocol == id2.protocol);
	}
};

#ifdef TASVIR

#include "tasvir.h"
typedef CuckooMap<FlowId, Policy, Hash, EqualTo, TasvirMarker> OpenFlowTable;
OpenFlowTable *create_flow_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us);
const OpenFlowTable *attach_flow_table(tasvir_area_desc *root_desc, int id);

#else

typedef CuckooMap<FlowId, Policy, Hash, EqualTo> OpenFlowTable;
OpenFlowTable *create_flow_table(int id);
const OpenFlowTable *attach_flow_table(int id);

#endif // ifdef TASVIR

#endif // #ifndef _FLOW_TABLE__H_
