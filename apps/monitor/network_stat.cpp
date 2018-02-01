#include "network_stat.h"

tasvir_area_desc *allocate_global_stats(tasvir_area_desc *root_desc,
		uint64_t int_us, uint64_t ext_us) {
	tasvir_area_desc param = {};
	param.pd = root_desc;
	param.owner = NULL;
	param.type = TASVIR_AREA_TYPE_APP;
	param.len = sizeof(struct network_stat);
	param.sync_int_us = int_us;
	param.sync_ext_us = ext_us;
	snprintf(param.name, sizeof(param.name), "statistics");

	tasvir_area_desc *desc = tasvir_new(param);
	if (!desc) {
		fprintf(stderr, "tasvir_new '%s' failed\n", param.name);
		return NULL;
	}

	printf("Created global memoryzone: %s (size: %lu)\n", param.name, sizeof(struct network_stat));

	return desc;
}

tasvir_area_desc *allocate_local_stats(tasvir_area_desc *root_desc, int local_id,
		uint64_t int_us, uint64_t ext_us) {
	tasvir_area_desc param = {};
	param.pd = root_desc;
	param.owner = NULL;
	param.type = TASVIR_AREA_TYPE_APP;
	param.len = sizeof(struct network_stat);
	param.sync_int_us = int_us;
	param.sync_ext_us = ext_us;
	snprintf(param.name, sizeof(param.name), "statistics_%02d", local_id);

	tasvir_area_desc *desc = tasvir_new(param);
	if (!desc) {
		fprintf(stderr, "tasvir_new '%s' failed\n", param.name);
		return NULL;
	}

	printf("Created local memoryzone: %s (size: %lu)\n", param.name, sizeof(struct network_stat));

	return desc;
}

const struct network_stat *attach_global_stats(tasvir_area_desc *root_desc) {
	char memzone_name[32];
	snprintf(memzone_name, sizeof(memzone_name), "statistics");
	tasvir_area_desc *desc= 
		tasvir_attach_wait(root_desc, memzone_name, NULL, false, 5 * 1000 * 1000);
	if (!desc)
		return NULL;

	return (struct network_stat *)tasvir_data(desc);
}

const struct network_stat *attach_local_stats(tasvir_area_desc *root_desc, int id) {
	char memzone_name[32];
	snprintf(memzone_name, sizeof(memzone_name), "statistics_%02d", id);
	tasvir_area_desc *desc= 
		tasvir_attach_wait(root_desc, memzone_name, NULL, false, 5 * 1000 * 1000);
	if (!desc)
		return NULL;

	return (struct network_stat *)tasvir_data(desc);
}

