#ifndef _ASSET__H_
#define _ASSET__H_
#pragma once

#include <functional>

#define TASVIR
#include "cuckoo_map_lock.h"
#include "tasvir.h"

#define MIN(A,B) ((A<B)?(A):(B))
#define MAX(A,B) ((A>B)?(A):(B))

#define PRADS_SERVICE_CNT 10
#define PRADS_OS_CNT 4

/*
 * Data structures are borrowed from PRADS
 */

struct ServiceAsset {
	uint16_t sigidx;
	double first_seen;
	double last_seen;
	uint32_t detected_count;
	uint16_t port;
	uint8_t proto;
};

struct OsAsset {
	uint16_t sigidx;
	double first_seen;
	double last_seen;
	uint32_t detected_count;
};

struct Asset {
	uint32_t ip_addr;
	double first_seen;
	double last_seen;
	uint32_t detected_count;
	OsAsset oasset[PRADS_OS_CNT];
	ServiceAsset sasset[PRADS_SERVICE_CNT];
};

struct IntHash {
	uint32_t operator()(uint32_t id) const {
		union S {
			uint32_t u32;
			uint8_t u8[4];
		};

		S key = {id};
		size_t i = 0;
		uint32_t hash = 0;
		while (i != 4) {
			hash += key.u8[i++];
			hash += hash << 10;
			hash ^= hash >> 6;
		}
		hash += hash << 3;
		hash ^= hash >> 11;
		hash += hash << 15;

		return hash;
	}
};

typedef CuckooMap<uint32_t /* IP */, Asset, IntHash> AssetTable;

tasvir_area_desc *allocate_global_asset_table(tasvir_area_desc *root_desc,
		uint64_t int_us, uint64_t ext_us);
const AssetTable *attach_global_asset_table(tasvir_area_desc *root_desc);
tasvir_area_desc *allocate_local_asset_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us);
const AssetTable *attach_local_asset_table(tasvir_area_desc *root_desc, int id);

static void aggregate_asset(Asset *first, const Asset *second) {
	first->first_seen = MIN(first->first_seen, second->first_seen);
	first->last_seen = MAX(first->last_seen, second->last_seen);
	first->detected_count += second->detected_count;

	for (int i = 0; i < PRADS_OS_CNT; i++) {
		OsAsset *a = &first->oasset[i];
		const OsAsset *b = &second->oasset[i];
		if (a->detected_count && b->detected_count) {
			a->first_seen = MIN(a->first_seen, b->first_seen);
			a->last_seen = MAX(a->last_seen, b->last_seen);
			a->detected_count += b->detected_count;
		} else if (b->detected_count) {
			memcpy(a, b, sizeof(OsAsset));
		}
	}

	for (int i = 0; i < PRADS_SERVICE_CNT; i++) {
		ServiceAsset *a = &first->sasset[i];
		const ServiceAsset *b = &second->sasset[i];
		if (a->detected_count && b->detected_count) {
			a->first_seen = MIN(a->first_seen, b->first_seen);
			a->last_seen = MAX(a->last_seen, b->last_seen);
			a->detected_count += b->detected_count;
		} else if (b->detected_count) {
			memcpy(a, b, sizeof(ServiceAsset));
		}
	}

	return;	
}

#endif // #ifndef _ASSET__H_
