#include "connection.h"

#define MAX_BUCKETS (64 * 1024)
#define MAX_ENTRIES (MAX_BUCKETS * 4)

tasvir_area_desc *allocate_global_connection_table(tasvir_area_desc *root_desc,
		uint64_t int_us, uint64_t ext_us) {
	size_t table_size = ConnectionTable::Size(MAX_ENTRIES, MAX_BUCKETS);

	tasvir_area_desc param = {};
	param.pd = root_desc;
	param.owner = NULL;
	param.type = TASVIR_AREA_TYPE_APP;
	param.len = table_size;
	param.sync_int_us = int_us;
	param.sync_ext_us = ext_us;
	snprintf(param.name, sizeof(param.name), "connection");

	tasvir_area_desc *desc = tasvir_new(param);
	if (!desc) {
		fprintf(stderr, "tasvir_new '%s' failed\n", param.name);
		return NULL;
	}

	auto table = ConnectionTable::Create(tasvir_data(desc), table_size,
			MAX_ENTRIES, MAX_BUCKETS);
	printf("Created global memoryzone: %s (size: %lu)\n", param.name, table_size);

	return desc;
}

const ConnectionTable *attach_global_connection_table(tasvir_area_desc *root_desc) {
	char memzone_name[32];
	snprintf(memzone_name, sizeof(memzone_name), "connection");
	tasvir_area_desc *desc=
		tasvir_attach_wait(root_desc, memzone_name, NULL, false, 5 * 1000 * 1000);
	if (!desc)
		return NULL;

	return (const ConnectionTable *)tasvir_data(desc);
}

tasvir_area_desc *allocate_local_connection_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us) {
	size_t table_size = ConnectionTable::Size(MAX_ENTRIES, MAX_BUCKETS);

	tasvir_area_desc param = {};
	param.pd = root_desc;
	param.owner = NULL;
	param.type = TASVIR_AREA_TYPE_APP;
	param.len = table_size;
	param.sync_int_us = int_us;
	param.sync_ext_us = ext_us;
	snprintf(param.name, sizeof(param.name), "connection_%02d", id);

	tasvir_area_desc *desc = tasvir_new(param);
	if (!desc) {
		fprintf(stderr, "tasvir_new '%s' failed\n", param.name);
		return NULL;
	}

	auto table = ConnectionTable::Create(tasvir_data(desc), table_size,
			MAX_ENTRIES, MAX_BUCKETS);
	printf("Created local memoryzone: %s (size: %lu)\n", param.name, table_size);

	return desc;
}

const ConnectionTable *attach_local_connection_table(tasvir_area_desc *root_desc, int id) {
	char memzone_name[32];
	snprintf(memzone_name, sizeof(memzone_name), "connection_%02d", id);
	tasvir_area_desc *desc=
		tasvir_attach_wait(root_desc, memzone_name, NULL, false, 5 * 1000 * 1000);
	if (!desc)
		return NULL;

	return (const ConnectionTable *)tasvir_data(desc);
}

