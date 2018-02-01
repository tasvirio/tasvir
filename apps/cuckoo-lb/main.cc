#include "cuckoo_map.h"
#include "generate_lut.h"
#include "tasvir.h"
#include <assert.h>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdio.h>
#include <time.h>

// Consts
const size_t BUCKETS = 1 << 18;  // Tune this to change pressure
const size_t ENTRIES = BUCKETS * 4;

// Make sure these two are divisible
// FLOWS_IN_SSYTEM controls the overall number of flows serviced by the system.
const size_t FLOWS_IN_SYSTEM = 1000000;
// REPLACEMENT_RATE controls the number of new flow arrivals each second.
const size_t REPLACEMENT_RATE = 1000;  // This is per second replacement
const size_t LUT_SIZE = 2048;          // Size is this times sizeof(size_t) * 2
const size_t MAX_LBS = 1024;
const int BATCH_SIZE = 32;
const size_t ADDR_RANGE = 1000000;
// How often to report.
#define REPORT_SECS 1
// How many seconds to run.
#define RUN_SECS 30
typedef CuckooMap<uint64_t, uint64_t, std::hash<uint64_t>,
                  std::equal_to<uint64_t>, TasvirMarker>
    ConnectionTable;

tasvir_area_desc *lut_backend_area;
tasvir_area_desc *lut_lb_area;
// This is really data, but defined globally because many things access them.
// Tasvir areas
tasvir_area_desc *target_areas[MAX_LBS];
// Tables that live in the above Tasvir area.
ConnectionTable *tables[MAX_LBS];
// LUT table for deciding the DIP to which a connection is sent.
size_t *lut_backend;
// LUT table for deciding which backend accesses the current data.
size_t *lut_lb;
// ID of this connection.
size_t id;
bool init;
size_t served;
size_t writes;
size_t removes;
size_t fills;
size_t rpc_calls = 0;
size_t lookup_rpcs = 0;
size_t rpc_recd = 0;

inline void cpu_relax() {
  __asm__ __volatile__("rep; nop" : : : "memory");
}

static inline uint32_t fast_random() {
  static uint64_t seed = 42;
  seed = seed * 1103515245 + 12345;
  return seed >> 32;
}

static inline uint32_t fast_random1() {
  static uint64_t seed = 31;
  seed = seed * 1103515245 + 12345;
  return seed >> 32;
}

static void rpc_find_be(void *v, std::ptrdiff_t *o);
static inline size_t find_backend(size_t owner, size_t tbl, size_t address) {
  auto entry = tables[tbl]->Find(address);
  served++;  // Account for packets here
  if (entry == nullptr) {
    if (id == owner) {
      auto idx = address % LUT_SIZE;
      auto target = lut_backend[idx];
      auto insert = tables[tbl]->Insert(address, target);
      writes++;
      if (insert == nullptr) {
        // If we can't find an entry we just clear the table.
        // In reality we should be looking for entries to evict, but
        // laziness.
        fills++;
      }
      return target;
    } else {
      tasvir_rpc(target_areas[tbl], &rpc_find_be, address);
      lookup_rpcs++;
      rpc_calls++;
    }
  } else {
    return entry->second;
  }
  return 0;
}

static inline void remove_address(size_t tbl, size_t address) {
  auto entry = tables[tbl]->Find(address);
  if (entry != nullptr) {
    removes++;
    tables[tbl]->Remove(address);
  }
}

static void rpc_rem(void *v, std::ptrdiff_t *o) {
  uint8_t *bv = (uint8_t *)v;
  size_t *ret = (size_t *)(&bv[0]);
  size_t arg = *(size_t *)(&bv[o[0]]);
  auto idx = arg % LUT_SIZE;
  auto target = lut_lb[idx];
  rpc_recd++;
  *ret = 0;
  if (!init) {
    return;
  }
  remove_address(target, arg);
}

static void rpc_find_be(void *v, std::ptrdiff_t *o) {
  uint8_t *bv = (uint8_t *)v;
  size_t *ret = (size_t *)(&bv[0]);
  size_t arg = *(size_t *)(&bv[o[0]]);
  auto idx = arg % LUT_SIZE;
  auto target = lut_lb[idx];
  rpc_recd++;
  if (!init) {
    *ret = 0;
    return;
  }
  *ret = find_backend(id, target, arg);
}

static void generate_flows(size_t *flows, size_t *deleted, size_t change) {
  for (size_t i = 0; i < change; i++) {
    deleted[i] = flows[i];
    flows[i] = fast_random1() % ADDR_RANGE;
  }
}

static void test(size_t id, size_t lbs, __unused size_t partitions) {
  clock_t now = clock();
  served = 0;
  rpc_calls = 0;
  rpc_recd = 0;
  writes = 0;
  lookup_rpcs = 0;
  size_t *flows = new size_t[FLOWS_IN_SYSTEM];
  size_t *deleted = new size_t[REPLACEMENT_RATE];
  int seconds_run = 0;
  for (size_t i = 0; i < FLOWS_IN_SYSTEM; i++) {
    flows[i] = fast_random1() % ADDR_RANGE;
  }
  size_t replacement_index = 0;
  size_t replacement_groups = FLOWS_IN_SYSTEM / REPLACEMENT_RATE;
  size_t to_delete = 0;
  size_t del_rpcs = 0;
  while (true) {
    for (int batch_idx = 0; batch_idx < BATCH_SIZE; batch_idx++) {
      // Generate a value.
      auto val = flows[fast_random() % FLOWS_IN_SYSTEM];
      auto idx = val % LUT_SIZE;
      auto target = lut_lb[idx];
      find_backend(target % lbs, target, val);
    }
    tasvir_service();
    clock_t current = clock();
    clock_t secs = ((current - now) / CLOCKS_PER_SEC);
    if (secs >= REPORT_SECS) {
      std::cout << secs << "  " << served << " rec " << rpc_recd << " sen "
                << rpc_calls << " writes " << writes << " full " << fills
                << " dels " << removes << "   " << lookup_rpcs << "   "
                << del_rpcs << std::endl;
      seconds_run++;
      if (seconds_run >= RUN_SECS) {
        std::cout << "Have run for " << seconds_run << " seconds done"
                  << std::endl;
        return;
      }

      generate_flows(&flows[replacement_index * REPLACEMENT_RATE], deleted,
                     REPLACEMENT_RATE);
      to_delete = REPLACEMENT_RATE;
      replacement_index = (replacement_index + 1) % replacement_groups;
      now = current;
      served = 0;
      rpc_calls = 0;
      rpc_recd = 0;
      served = 0;
      writes = 0;
      fills = 0;
      removes = 0;
      lookup_rpcs = 0;
      del_rpcs = 0;
      for (size_t rem = 0; rem < to_delete; rem++) {
        auto val = deleted[rem];
        auto idx = val % LUT_SIZE;
        auto target = lut_lb[idx];
        // auto target = id;
        if (target % lbs == id) {
          remove_address(target, val);
        } else {
          tasvir_rpc(target_areas[target], &rpc_rem, val);
          rpc_calls++;
          del_rpcs++;
        }
        if (rem % BATCH_SIZE) {
          tasvir_service();
        }
      }
      tasvir_service();
    }
  };
}

void allocate_table(tasvir_area_desc *root_desc, size_t id) {
  char area_name[32];
  tasvir_area_desc param = {};
  param.pd = root_desc;
  param.owner = NULL;
  param.type = TASVIR_AREA_TYPE_APP;
  size_t len = ConnectionTable::Size(ENTRIES, BUCKETS);
  param.len = len;
  std::cerr << "Requested area required "
            << ConnectionTable::Size(ENTRIES, BUCKETS) << std::endl;
  // Naming based on ID.
  snprintf(area_name, 32, "dict%lu", id);
  strcpy(param.name, area_name);
  target_areas[id] = tasvir_new(param);  // Created a region.
  assert(target_areas[id]);
}

void create_table(__unused tasvir_area_desc *root_desc, size_t id) {
  size_t len = ConnectionTable::Size(ENTRIES, BUCKETS);
  void *data = tasvir_data(target_areas[id]);
  tables[id] = ConnectionTable::Create(data, len, ENTRIES, BUCKETS);
}

void attach_table(tasvir_area_desc *root_desc, size_t id) {
  char area_name[32];
  snprintf(area_name, 32, "dict%lu", id);
  std::cout << "Waiting to attach to " << area_name << std::endl;
  target_areas[id] =
      tasvir_attach_wait(root_desc, area_name, NULL, false, 600 * 1000 * 1000);
  std::cout << "Attached to " << area_name << std::endl;
  tables[id] = (ConnectionTable *)tasvir_data(target_areas[id]);
}

static inline void busy_sleep(clock_t millis) {
  clock_t duration = millis * CLOCKS_PER_SEC / 1000;
  clock_t start = clock();
  clock_t now = start;
  std::cout << "Hello I am sleeping beauty I sleep for " << millis << " ms "
            << std::endl;
  do {
    now = clock();
    tasvir_service();
    cpu_relax();
  } while (now - start < duration);
}

int main(int argc, char *argv[]) {
  std::cout << "Running cuckoo bench" << std::endl;
  if (argc < 5) {
    std::cerr << "Usage " << argv[0] << " master core lbs [backends]"
              << std::endl;
    std::cerr << "id: <int> process ID 0 is master" << std::endl;
    std::cerr << "core: <int> core to use" << std::endl;
    std::cerr << "lb_partitions: <int> how many load balancer partitions "
              << "use more than 2 -- otherwise things are sad" << std::endl;
    std::cerr << "lbs: <int> how many load balancers" << std::endl;
    std::cerr << "backends: <int> how many backends, only needed for master"
              << " use more than 2 otherwise things are sad" << std::endl;
    std::cerr << "Got " << argc << " args" << std::endl;
    for (int i = 0; i < argc; i++) {
      std::cerr << i << " " << argv[i] << std::endl;
    }
    abort();
  }

  id = std::strtoul(argv[1], NULL, 10);
  unsigned long core = std::strtoul(argv[2], NULL, 10);
  unsigned long lb_partitions = std::strtoul(argv[3], NULL, 10);
  unsigned long lbs = std::strtoul(argv[4], NULL, 10);
  unsigned long backends = 0;
  if (id == 0 && argc < 6) {
    std::cerr << "Need number of backends" << std::endl;
    abort();
  }
  if (id == 0) {
    backends = std::strtoul(argv[5], NULL, 10);
  }
  std::cout << "Running on core " << core
            << (id == 0 ? " as master" : "as worker") << std::endl;
  tasvir_area_desc *root_desc = tasvir_init(TASVIR_THREAD_TYPE_APP, core, NULL);
  if (!root_desc) {
    std::cerr << "tasvir_init failed" << std::endl;
    abort();
  }
  std::cerr << "tasvir_init_done" << std::endl;
  tasvir_area_desc param = {};
  char area_name[32];

  // First create LUTs
  init = false;
  if (id == 0) {
    param.pd = root_desc;
    param.owner = NULL;
    param.type = TASVIR_AREA_TYPE_APP;
    param.len = LUT_SIZE * sizeof(size_t);
    snprintf(area_name, 32, "lut_backend");
    strcpy(param.name, area_name);
    lut_backend_area = tasvir_new(param);  // Created a region.
    assert(lut_backend_area);
    lut_backend = (size_t *)tasvir_data(lut_backend_area);
    tasvir_service();
    param.pd = root_desc;
    param.owner = NULL;
    param.type = TASVIR_AREA_TYPE_APP;
    param.len = LUT_SIZE * sizeof(size_t);
    snprintf(area_name, 32, "lut_lbs");
    strcpy(param.name, area_name);
    lut_lb_area = tasvir_new(param);  // Created a region.
    assert(lut_lb_area);
    lut_lb = (size_t *)tasvir_data(lut_lb_area);
    tasvir_service();
  }
  // Register for RPC
  tasvir_fn_info fn;  // = {
  fn.fnptr = &rpc_find_be;
  strcpy(fn.name, "find_backend");
  fn.oneway = 1;
  fn.fid = 22;
  fn.argc = 1;
  fn.ret_len = sizeof(size_t);
  fn.arg_lens[0] = sizeof(size_t);
  tasvir_rpc_register(&fn);
  // Register for RPC
  tasvir_fn_info fn2;  // = {
  fn2.fnptr = &rpc_rem;
  strcpy(fn2.name, "remove_address");
  fn2.oneway = 1;
  fn2.fid = 23;
  fn2.argc = 1;
  fn2.ret_len = sizeof(size_t);
  fn2.arg_lens[0] = sizeof(size_t);
  tasvir_rpc_register(&fn2);

  for (size_t i = id; i < lb_partitions; i += lbs) {
    allocate_table(root_desc, i);
  }

  for (size_t i = 0; i < lb_partitions; i++) {
    if (i % lbs != id) {
      attach_table(root_desc, i);
    }
  }

  if (id != 0) {
    snprintf(area_name, 32, "lut_backend");
    std::cout << "Waiting to attach to lut_backend" << std::endl;
    lut_backend_area = tasvir_attach_wait(root_desc, area_name, NULL, false,
                                          600 * 1000 * 1000);
    std::cout << "Attached to lut_backend" << std::endl;
    lut_backend = (size_t *)tasvir_data(lut_backend_area);

    snprintf(area_name, 32, "lut_lbs");
    std::cout << "Waiting to attach to lut_lbs" << std::endl;
    lut_lb_area = tasvir_attach_wait(root_desc, area_name, NULL, false,
                                     600 * 1000 * 1000);
    std::cout << "Attached to lut_lbs" << std::endl;
    lut_lb = (size_t *)tasvir_data(lut_lb_area);
  }

  if (id == 0) {
    busy_sleep(lbs * 100);
    std::cout << "Begin generating LB LUT size=" << LUT_SIZE * sizeof(size_t)
              << std::endl;
    generate_lut("lbs", lb_partitions, lut_lb, LUT_SIZE);
    tasvir_log_write(lut_lb, LUT_SIZE * sizeof(size_t));
    std::cout << "Done generating lbs LUT" << std::endl;
    std::cout << "Begin generating backend LUT" << std::endl;
    generate_lut("backend", backends, lut_backend, LUT_SIZE);
    tasvir_log_write(lut_backend, LUT_SIZE * sizeof(size_t));
    std::cout << "Done generating backend LUT" << std::endl;
    std::cout << "LUT area created; versions are " << lut_lb_area->h->version
              << "   " << lut_backend_area->h->version << std::endl;
  }
  std::cout << "Attached to LUT areas " << std::endl;

  std::cout << "Beginning to wait for version change in lut" << std::endl;
  /*
  do {
    tasvir_service();
    cpu_relax();
  } while ((lut_backend_area->h->version < 3 || lut_lb_area->h->version < 3));
  std::cerr << "LUT version requirements met" << std::endl;

  busy_sleep((lbs - id) * 50);
  for (size_t i = id; i < lb_partitions; i += lbs) {
    create_table(root_desc, i);
    busy_sleep(500);
  }
  */
  if (id == 0) {
    busy_sleep(1000);
  }

  for (size_t i = 0; i < lb_partitions; i++) {
    if (i % lbs == id) {
      create_table(root_desc, i);
    }
    std::cout << "Waiting for table " << i << " to get ready" << std::endl;
    do {
      tasvir_service();
      cpu_relax();
    } while (!tables[i]->IsReady());
    std::cout << "Table " << i << " ready" << std::endl;
  }
  std::cout << "Attached to all areas " << std::endl;
  // std::cout << "Cleared" << std::endl;
  // cuckoo->Clear();
  init = true;
  std::cout << "Starting to serve" << std::endl;
  test(id, lbs, lb_partitions);
}
