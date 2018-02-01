#ifndef _MONITOR__H_
#define _MONITOR__H_

#include "asset.h"
#include "connection.h"

struct monitor_results {
	struct network_stat *stat;
	AssetTable *asset_table;
	ConnectionTable *conn_table;
};

void monitor_packet(struct rte_mbuf *mbuf, struct monitor_results *);

#endif // #ifndef _MONITOR__H_
