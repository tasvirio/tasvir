#include "net_table.h"

Forward *create_forwarding_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us, int switch_cnt, int arr_cnt) {
	size_t table_size = sizeof(Forward) * switch_cnt * arr_cnt;

	tasvir_area_desc param = {};
	param.pd = root_desc;
	param.owner = NULL;
	param.type = TASVIR_AREA_TYPE_APP;
	param.len = table_size;
	param.sync_int_us = int_us;
	param.sync_ext_us = ext_us;
	snprintf(param.name, sizeof(param.name), "forward_table_%02d", id);

	tasvir_area_desc *desc = tasvir_new(param);
	if (!desc) {
		fprintf(stderr, "tasvir_new '%s' failed\n", param.name);
		return NULL;
	}

	memset(tasvir_data(desc), 0, sizeof(table_size));
	printf("Created net table: %s (%lu)\n", param.name, table_size);

	return (Forward *)tasvir_data(desc);
}

const Forward *attach_forwarding_table(tasvir_area_desc *root_desc, int id) {
	char memzone_name[32];
	snprintf(memzone_name, sizeof(memzone_name), "forward_table_%02d", id);
	tasvir_area_desc *desc=
		tasvir_attach_wait(root_desc, memzone_name, NULL, false, 5 * 1000 * 1000);
	if (!desc) {
		fprintf(stderr, "tasvir_attach '%s' failed\n", memzone_name);
		return NULL;
	}
	printf("Attach net table: %s\n", memzone_name);

	return (const Forward *)tasvir_data(desc);
}

Link *create_adjacency_table(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us, int arr_cnt) {
	size_t table_size = sizeof(Link) * NEIGHBOR_PER_SWITCH * arr_cnt;
	tasvir_area_desc param = {};
	param.pd = root_desc;
	param.owner = NULL;
	param.type = TASVIR_AREA_TYPE_APP;
	param.len = table_size;
	param.sync_int_us = int_us;
	param.sync_ext_us = ext_us;
	snprintf(param.name, sizeof(param.name), "table_%02d", id);

	tasvir_area_desc *desc = tasvir_new(param);
	if (!desc) {
		fprintf(stderr, "tasvir_new '%s' failed\n", param.name);
		return NULL;
	}

	memset(tasvir_data(desc), 0, sizeof(table_size));
	printf("Created net table: %s (%lu)\n", param.name, table_size);

	return (Link *)tasvir_data(desc);
}

const Link *attach_adjacency_table(tasvir_area_desc *root_desc, int id) {
	char memzone_name[32];
	snprintf(memzone_name, sizeof(memzone_name), "table_%02d", id);
	tasvir_area_desc *desc=
		tasvir_attach_wait(root_desc, memzone_name, NULL, false, 5 * 1000 * 1000);
	if (!desc) {
		fprintf(stderr, "tasvir_attach '%s' failed\n", memzone_name);
		return NULL;
	}
	printf("Attach adjacency table: %s\n", memzone_name);

	return (const Link *)tasvir_data(desc);
}
