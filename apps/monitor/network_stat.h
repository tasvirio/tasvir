#ifndef _NETWORK_STAT__H_ 
#define _NETWORK_STAT__H_ 
#pragma once

#include <stdint.h>

#include "tasvir.h"

/* 
 * Data structures are borrowed from PRADS 
 */

struct network_stat {
  uint32_t got_packets;   /* number of packets received by prads */
  uint32_t eth_recv;      /* number of Ethernet packets received */
  uint32_t arp_recv;      /* number of ARP packets received */
  uint32_t otherl_recv;   /* number of other Link layer packets received */
  uint32_t vlan_recv;     /* number of VLAN packets received */
  uint32_t ip4_recv;      /* number of IPv4 packets received */
  uint32_t ip6_recv;      /* number of IPv6 packets received */
  uint32_t ip4ip_recv;    /* number of IP4/6 packets in IPv4 packets */
  uint32_t ip6ip_recv;    /* number of IP4/6 packets in IPv6 packets */
  uint32_t gre_recv;      /* number of GRE packets received */
  uint32_t tcp_recv;      /* number of tcp packets received */
  uint32_t udp_recv;      /* number of udp packets received */
  uint32_t icmp_recv;     /* number of icmp packets received */
  uint32_t othert_recv;   /* number of other transport layer packets received */
  uint32_t assets;        /* total number of assets detected */
  uint32_t tcp_os_assets; /* total number of tcp os assets detected */
  uint32_t udp_os_assets; /* total number of udp os assets detected */
  uint32_t icmp_os_assets; /* total number of icmp os assets detected */
  uint32_t dhcp_os_assets; /* total number of dhcp os assets detected */
  uint32_t tcp_services;   /* total number of tcp services detected */
  uint32_t tcp_clients;    /* total number of tcp clients detected */
  uint32_t udp_services;   /* total number of udp services detected */
  uint32_t udp_clients;    /* total number of tcp clients detected */
};


// allocate aggregated statistics memory zone
tasvir_area_desc *allocate_global_stats(tasvir_area_desc *root_desc,
		uint64_t int_us, uint64_t ext_us);
// allocate per-instance (with 'id') local statistics memory zone with 
tasvir_area_desc *allocate_local_stats(tasvir_area_desc *root_desc, int id,
		uint64_t int_us, uint64_t ext_us);
// attach global statistics (read-only)
const struct network_stat *attach_global_stats(tasvir_area_desc *root_desc);
// attach local statistics of instance 'id' (read-only)
const struct network_stat *attach_local_stats(tasvir_area_desc *root_desc, int id);

#endif // #ifndef _NETWORK_STAT__H_
