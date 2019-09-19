#include "cuckoo_map.h"
#include "tasvir.h"
#include <assert.h>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdio.h>
#include <time.h>

static inline uint32_t fast_random() {
  static uint64_t seed = 42;
  seed = seed * 1103515245 + 12345;
  return seed >> 32;
}

// 256 MB Tasvir area
const size_t AREA_SIZE = 256ull * 1024ull * 1024ull;
const size_t BUCKETS = 1 << 18;
const size_t ENTRIES = BUCKETS * 4;  // 75% occupancy, should increase.
int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage " << argv[0] << " worker_id" << std::endl;
    abort();
  }

  auto wid = std::strtoul(argv[1], NULL, 10);
  tasvir_area_desc *root_desc = tasvir_init();
  if (!root_desc) {
    std::cerr << "tasvir_init failed" << std::endl;
    abort();
  }
  tasvir_area_desc param = {};
  param.pd = root_desc;
  param.owner = NULL;
  param.len = AREA_SIZE;
  char area_name[32];
  // Naming based on ID.
  snprintf(area_name, 32, "dict%lu", wid);
  strcpy(param.name, area_name);
  tasvir_area_desc *d = tasvir_new(param);  // Created a region.
  assert(d);
  void *data = tasvir_data(d);
  auto cuckoo_t =
      CuckooMap<uint64_t, uint64_t, std::hash<uint64_t>,
                std::equal_to<uint64_t>, TasvirMarker>::Create(data, AREA_SIZE,
                                                               ENTRIES,
                                                               BUCKETS);

  void *heap = malloc(AREA_SIZE * 100);
  auto cuckoo_m =
      CuckooMap<uint64_t, uint64_t>::Create(heap, AREA_SIZE, ENTRIES, BUCKETS);
  const size_t BENCH_LENGTH = 2 * 10000000;
  const size_t WARMUP = 10000;
  const size_t SERVICE_DURATION = 64;
  while (true) {
    for (size_t i = 0; i < WARMUP; i++) {
      auto val = fast_random() % 500000;
      auto entry = cuckoo_m->Find(val);
      if (entry == nullptr) {
        auto insert = cuckoo_m->Insert(val, val);
        if (insert == nullptr) {
          cuckoo_m->Clear();
        }
      }
    }
    std::cout << "Starting memory bench" << std::endl;
    clock_t start = clock();
    for (size_t i = 0; i < BENCH_LENGTH; i++) {
      auto val = fast_random() % 500000;
      auto entry = cuckoo_m->Find(val);
      if (entry == nullptr) {
        auto insert = cuckoo_m->Insert(val, val);
        if (insert == nullptr) {
          // cuckoo_m->Clear();
        }
      }
    }
    clock_t end = clock();
    std::cout << "Bench: " << BENCH_LENGTH << " without Tasvir took "
              << (end - start) << " " << CLOCKS_PER_SEC << " "
              << (BENCH_LENGTH / (end - start)) * (CLOCKS_PER_SEC / 1000000)
              << " mqps" << std::endl;

    for (size_t i = 0; i < WARMUP; i++) {
      auto val = fast_random() % 500000;
      auto entry = cuckoo_t->Find(val);
      if (entry == nullptr) {
        auto insert = cuckoo_t->Insert(val, val);
        if (insert == nullptr) {
          cuckoo_t->Clear();
        }
      }
      if (i % SERVICE_DURATION == 0) {
        tasvir_service();
      }
    }
    std::cout << "Starting Tasvir (no service) bench" << std::endl;
    start = clock();
    for (size_t i = 0; i < BENCH_LENGTH; i++) {
      auto val = fast_random() % 500000;
      auto entry = cuckoo_t->Find(val);
      if (entry == nullptr) {
        auto insert = cuckoo_t->Insert(val, val);
        if (insert == nullptr) {
          // cuckoo_t->Clear();
        }
      }
      // if (i % SERVICE_DURATION == 0) {
      // tasvir_service();
      //}
    }
    end = clock();
    tasvir_service();
    std::cout << "Bench: " << BENCH_LENGTH << " with Tasvir (no service) took "
              << (end - start) << " " << CLOCKS_PER_SEC << " "
              << (BENCH_LENGTH / (end - start)) * (CLOCKS_PER_SEC / 1000000)
              << " mqps" << std::endl;
    cuckoo_t->Clear();
    tasvir_service();
    for (size_t i = 0; i < WARMUP; i++) {
      auto val = fast_random() % 500000;
      auto entry = cuckoo_t->Find(val);
      if (entry == nullptr) {
        auto insert = cuckoo_t->Insert(val, val);
        if (insert == nullptr) {
          cuckoo_t->Clear();
        }
      }
      if (i % SERVICE_DURATION == 0) {
        tasvir_service();
      }
    }
    std::cout << "Starting Tasvir bench" << std::endl;
    start = clock();
    for (size_t i = 0; i < BENCH_LENGTH; i++) {
      auto val = fast_random() % 500000;
      auto entry = cuckoo_t->Find(val);
      if (entry == nullptr) {
        auto insert = cuckoo_t->Insert(val, val);
        if (insert == nullptr) {
          // cuckoo_t->Clear();
        }
      }
      if (i % SERVICE_DURATION == 0) {
        tasvir_service();
      }
    }
    end = clock();
    tasvir_service();
    std::cout << "Bench: " << BENCH_LENGTH << " with Tasvir (yes service) took "
              << (end - start) << " " << CLOCKS_PER_SEC << " "
              << (BENCH_LENGTH / (end - start)) * (CLOCKS_PER_SEC / 1000000)
              << " mqps" << std::endl;
    cuckoo_m->Clear();
    tasvir_service();
  }
}
