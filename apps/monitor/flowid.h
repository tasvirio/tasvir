#ifndef _FLOWID__H_
#define _FLOWID__H_

#include <stdint.h>
#include <stdlib.h>

typedef uint32_t HashResult;

struct FlowId {
	uint32_t src_ip;
	uint32_t dst_ip;
	uint16_t src_port;
	uint16_t dst_port;
	uint8_t protocol;
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

#endif // #ifndef _FLOWID__H_
