#include "cuckoo_map.h"
#include "tasvir.h"
#include <assert.h>
#include <iostream>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
/*#define _ALLOC_TEST_*/
#define CONSISTENCY 0
/*#define CONSISTENCY 1*/

#define MAX_CORES 4
#define MAX_SERVERS 4

struct FlowId {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint32_t src_port;
  uint32_t dst_port;
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

static inline uint32_t fast_random() {
  static uint64_t seed = 42;
  seed = seed * 1103515245 + 12345;
  return seed >> 32;
}

int main(__unused int argc, __unused char* argv[]) {
  size_t need_bytes =
      CuckooMap<size_t, uint64_t>::Size(128 * 4, 128);
  void* test = malloc(need_bytes);
  auto cuckoo = CuckooMap<size_t, uint64_t>::Create(test, need_bytes, 128 * 4, 128);
  size_t x = 10;
  while (true) {
      auto val = fast_random() % x;
      std::cout << "Processing " << val << std::endl;
      auto entry = cuckoo->Find(val);
      if (!entry) {
          cuckoo->Insert(val, 0);
          std::cout << "Inserted entry " << val << " for the first time" << std::endl << std::endl;
      } else {
          if (entry->first != val) {
              abort();
          }
          auto prev = entry->second;
          cuckoo->Remove(val);
          cuckoo->Insert(val, prev + 1);
          std::cout << "Inserted entry " << val << " for " << prev + 1 << " time" << std::endl << std::endl;
      }
  }
  //std::cout << "Need " << need_bytes << " of memory" << std::endl;
  //auto cuckoo = CuckooMap<FlowId, uint64_t, Hash, EqualTo>::Create(
      //test, need_bytes + 1, 4 * 64, 64);
  //cuckoo->Clear();
  //assert(cuckoo);
  //FlowId florida = {.src_ip = 484,
                    //.dst_ip = 224,
                    //.src_port = 22,
                    //.dst_port = 1234,
                    //.protocol = 55};
  //FlowId florida2 = {.src_ip = 424,
                    //.dst_ip = 221,
                    //.src_port = 22,
                    //.dst_port = 1234,
                    //.protocol = 55};
  //auto entry = cuckoo->Insert(florida, 64);
  //assert(entry != nullptr);
  //auto entry2 = cuckoo->Find(florida);
  //assert(entry == entry2);
  //assert(entry2->second == 64);
  //std::cout << entry->second << "   " << entry2->second << std::endl;
  //std::cout << entry->first.src_ip << "  " << entry2->first.src_ip << std::endl;

  //auto entry3 = cuckoo->Insert(florida2, 22);
  //auto entry4 = cuckoo->Insert(florida, 444);

  //auto entry5 = cuckoo->Find(florida);
  //assert(entry5->first.src_ip == 484 && entry5->second == 444);

  //auto entry6 = cuckoo->Find(florida2);
  //assert(entry6->first.src_ip == 424 && entry6->second == 22);

  //size_t stack_size = EntryStack<>::BytesForEntries(120);
  //auto stck = EntryStack<>::Create(malloc(stack_size), stack_size, 120);
  //for (int i = 0; i < 120; i++) {
      //int ret = stck->Push(i);
      //assert(ret == 1);
  //}
  //EntryIndex prev = 0;
  //bool once = false;
  //while (!stck->Empty()) {
      //EntryIndex t = stck->Top();
      //stck->Pop();
      //assert(t != prev);
      //once = true;
      //prev = t;
  //}
  //assert(once);

}
