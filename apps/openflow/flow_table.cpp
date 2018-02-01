#include "flow_table.h"

#ifdef TASVIR

OpenFlowTable *create_flow_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us) {
	size_t table_size = OpenFlowTable::Size(MAX_ENTRIES, MAX_BUCKETS);

	printf("Table size is %lu\n", table_size);

	tasvir_area_desc param = {};
	param.pd = root_desc;
	param.owner = NULL;
	param.type = TASVIR_AREA_TYPE_APP;
	param.len = table_size;
	param.sync_int_us = int_us;
	param.sync_ext_us = ext_us;
	snprintf(param.name, sizeof(param.name), "flowtable_%02d", id);

	tasvir_area_desc *desc = tasvir_new(param);
	if (!desc) {
		fprintf(stderr, "tasvir_new '%s' failed\n", param.name);
		return NULL;
	}

	OpenFlowTable* table = OpenFlowTable::Create(tasvir_data(desc), table_size,
			MAX_ENTRIES, MAX_BUCKETS);
	if (!table) {
		fprintf(stderr, "Fail to create openflow table\n");
		return NULL;
	}

	table->Clear();
	printf("Created flow table: %s\n", param.name);

	return table;
}

const OpenFlowTable *attach_flow_table(tasvir_area_desc *root_desc, int id) {
	char memzone_name[32];
	snprintf(memzone_name, sizeof(memzone_name), "flowtable_%02d", id);
	tasvir_area_desc *desc=
		tasvir_attach_wait(root_desc, memzone_name, NULL, false, 5 * 1000 * 1000);
	if (!desc)
		return NULL;

	return (OpenFlowTable *)tasvir_data(desc);
}

#else

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define START_ADDRESS 0x10000000

OpenFlowTable *create_flow_table(int id) {
	char memzone_name[32];
	snprintf(memzone_name, sizeof(memzone_name), "/tmp/flowtable_%02d", id);
	size_t table_size = OpenFlowTable::Size(MAX_ENTRIES, MAX_BUCKETS);

	// Create shared memory object
	int shm_fd = open(memzone_name, O_CREAT | O_RDWR | O_TRUNC, 0666);
	if (shm_fd < 0) {
		fprintf(stderr, "Fail to open\n");
		return NULL;
	}

	int ret = ftruncate(shm_fd, table_size);
	if (ret < 0) {
		fprintf(stderr, "Fail to truncate\n");
		return NULL;
	}

	void *table_ptr = mmap((void *) START_ADDRESS + table_size * (id - 1),
			table_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
			shm_fd, 0);
	if (!table_ptr){
		fprintf(stderr, "Fail to mmap\n");
		return NULL;
	}

	OpenFlowTable* table = OpenFlowTable::Create(table_ptr, table_size,
			MAX_ENTRIES, MAX_BUCKETS);
	if (!table) {
		fprintf(stderr, "Fail to create openflow table\n");
		return NULL;
	}

	table->Clear();
	printf("Created flow table: %s\n", memzone_name);

	return table;
}

const OpenFlowTable *attach_flow_table(int id) {
	char memzone_name[32];
	snprintf(memzone_name, sizeof(memzone_name), "/tmp/flowtable_%02d", id);
	size_t table_size = OpenFlowTable::Size(MAX_ENTRIES, MAX_BUCKETS);

	// Access to shared memory by read-only
	int shm_fd = open(memzone_name, O_RDONLY);
	if (shm_fd < 0) {
		fprintf(stderr, "Fail to open\n");
		return NULL;
	}

	void *table_ptr = mmap((void *) START_ADDRESS + table_size * (id - 1),
			table_size, PROT_READ, MAP_SHARED | MAP_FIXED, shm_fd, 0);
	if (!table_ptr){
		fprintf(stderr, "Fail to mmap\n");
		return NULL;
	}

	return (OpenFlowTable *)(table_ptr);
}

#endif
