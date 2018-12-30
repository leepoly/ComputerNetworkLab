#include "arpcache.h"
#include "arp.h"
#include "ether.h"
#include "packet.h"
#include "icmp.h"
#include "ip.h"
#include "rtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static arpcache_t arpcache;

extern rt_entry_t *longest_prefix_match(u32 ip);

// initialize IP->mac mapping, request list, lock and sweeping thread
void arpcache_init()
{
	bzero(&arpcache, sizeof(arpcache_t));

	init_list_head(&(arpcache.req_list));

	pthread_mutex_init(&arpcache.lock, NULL);

	pthread_create(&arpcache.thread, NULL, arpcache_sweep, NULL);
}

// release all the resources when exiting
void arpcache_destroy()
{
	pthread_mutex_lock(&arpcache.lock);

	struct arp_req *req_entry = NULL, *req_q;
	list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
		struct cached_pkt *pkt_entry = NULL, *pkt_q;
		list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
			list_delete_entry(&(pkt_entry->list));
			free(pkt_entry->packet);
			free(pkt_entry);
		}

		list_delete_entry(&(req_entry->list));
		free(req_entry);
	}

	pthread_kill(arpcache.thread, SIGTERM);

	pthread_mutex_unlock(&arpcache.lock);
}

// lookup the IP->mac mapping
//
// traverse the table to find whether there is an entry with the same IP
// and mac address with the given arguments
int arpcache_lookup(u32 ip4, u8 mac[ETH_ALEN])
{
	for (int i = 0; i < MAX_ARP_SIZE; i++) {
		if (arpcache.entries[i].ip4 == ip4) {
			memcpy(mac, arpcache.entries[i].mac, sizeof(u8) * ETH_ALEN);
			return 1;
		}
	}
	fprintf(stderr, "arpcache_lookup: lookup ip address in arp cache.\n");
	return 0;
}

// append the packet to arpcache
//
// Lookup in the list which stores pending packets, if there is already an
// entry with the same IP address and iface (which means the corresponding arp
// request has been sent out), just append this packet at the tail of that entry
// (the entry may contain more than one packet); otherwise, malloc a new entry
// with the given IP address and iface, append the packet, and send arp request.
void arpcache_append_packet(iface_info_t *iface, u32 ip4, char *packet, int len)
{
	fprintf(stderr, "append the ip address if lookup failed, and send arp request if necessary.\n");
	pthread_mutex_lock(&arpcache.lock);

	struct arp_req *req_entry = NULL, *req_q;
	struct arp_req* found_existed_req = NULL;

	struct cached_pkt* append_packet = (struct cached_pkt*)malloc(sizeof(struct cached_pkt));
	append_packet->packet = packet;
	append_packet->len = len;

	list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
		if (found_existed_req==NULL && req_entry->iface == iface && req_entry->ip4 == ip4) {
			found_existed_req = req_entry;
			list_add_tail(&append_packet->list, &req_entry->cached_packets);
		}
	}

	if (found_existed_req == NULL) {
		struct arp_req* new_arp_req = (struct arp_req*)malloc(sizeof(struct arp_req));
		new_arp_req->iface = iface;
		new_arp_req->ip4 = ip4;
		new_arp_req->retries = 0;
		init_list_head(&new_arp_req->cached_packets);
		list_add_tail(&append_packet->list, &new_arp_req->cached_packets);

		new_arp_req->sent = time(NULL);

		list_add_tail(&new_arp_req->list, &(arpcache.req_list));
		//printf("DEBUG\t");
		//printf(IP_FMT"\n", HOST_IP_FMT_STR(ip4));
		arp_send_request(iface, ip4);
	}

	pthread_mutex_unlock(&arpcache.lock);
}

// insert the IP->mac mapping into arpcache, if there are pending packets
// waiting for this mapping, fill the ethernet header for each of them, and send
// them out
void arpcache_insert(u32 ip4, u8 mac[ETH_ALEN])
{
	pthread_mutex_lock(&arpcache.lock);

	int free_entry_id = -1;
	for (int i = 0; i < MAX_ARP_SIZE; i++) {
		if (arpcache.entries[i].valid == 0) {
			free_entry_id = i;
			break;
		}
	}
	int replace_entry_id = (free_entry_id==-1? ((rand() % (MAX_ARP_SIZE - 0)) + 0) : free_entry_id);

	
	arpcache.entries[replace_entry_id].ip4 = ip4;
	memcpy(arpcache.entries[replace_entry_id].mac, mac, sizeof(u8) * ETH_ALEN);
	arpcache.entries[replace_entry_id].valid = 1;
	arpcache.entries[replace_entry_id].added = time(NULL);

	struct arp_req *req_entry = NULL, *req_q;

	list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
		
		if (req_entry->ip4 == ip4) {
			struct cached_pkt *cached_pkt_i = NULL, *cached_pkt_i_1;
			list_for_each_entry_safe(cached_pkt_i, cached_pkt_i_1, &(req_entry->cached_packets), list) {
				char *packet_i = cached_pkt_i->packet;
				struct ether_header *eh = (struct ether_header *)packet_i;
				memcpy(eh->ether_dhost, mac, sizeof(u8) * ETH_ALEN);

				iface_send_packet(req_entry->iface, packet_i, cached_pkt_i->len);
				fprintf(stderr, "arpcache_insert: insert ip->mac entry, and send all the pending packets.\n");
				list_delete_entry(&(cached_pkt_i->list));
				free(cached_pkt_i);
				//free(cached_pkt_i); already in iface_send_packet;
			}
			list_delete_entry(&(req_entry->list));
			free(req_entry);
		}
	}

	pthread_mutex_unlock(&arpcache.lock);
}

void refreshARPCacheEntry(u32 ip4, u8 *mac) {
	for (int i = 0; i < MAX_ARP_SIZE; i++) {
		if (arpcache.entries[i].valid==1 && arpcache.entries[i].ip4 == ip4) {
			memcpy(arpcache.entries[i].mac, mac, sizeof(u8)*ETH_ALEN);
			arpcache.entries[i].added = time(NULL);
		}
	}
}


// sweep arpcache periodically
//
// For the IP->mac entry, if the entry has been in the table for more than 15
// seconds, remove it from the table.
// For the pending packets, if the arp request is sent out 1 second ago, while 
// the reply has not been received, retransmit the arp request. If the arp
// request has been sent 5 times without receiving arp reply, for each
// pending packet, send icmp packet (DEST_HOST_UNREACHABLE), and drop these
// packets.
void *arpcache_sweep(void *arg) 
{
	while (1) {
		sleep(1);

		time_t now = time(NULL);
		pthread_mutex_lock(&arpcache.lock);
		for (int i = 0; i < MAX_ARP_SIZE; i++) {
			if (arpcache.entries[i].valid==1 && now-arpcache.entries[i].added > ARP_ENTRY_TIMEOUT) {
				arpcache.entries[i].valid = 0;
			}
		}

		struct arp_req *req_entry = NULL, *req_q;
		list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
			if (req_entry->retries == ARP_REQUEST_MAX_RETRIES) {
				struct cached_pkt *cached_pkt_i = NULL, *cached_pkt_q;
				//struct cached_pkt *cached_pkt_i = (struct cached_pkt *)((char *)&(req_entry->cached_packets) - offsetof(struct cached_pkt, list));
				//char *packet = cached_pkt_i->packet;
				list_for_each_entry_safe(cached_pkt_i, cached_pkt_q, &(req_entry->cached_packets), list) {
					char *packet = cached_pkt_i->packet;
					struct iphdr *ip = packet_to_ip_hdr(packet);
					struct ether_header *eh = (struct ether_header *)packet;

					
					//now need to lookup FIB to find the src IP
					rt_entry_t *src_rt_entry = longest_prefix_match(ntohl(ip->saddr));
					u32 ip4_on_router_recv = src_rt_entry->iface->ip;

					icmp_send_packet((const char *)ip, ip->ihl*4 + 8, 3, 1, ip4_on_router_recv, ntohl(ip->saddr), eh->ether_shost, eh->ether_dhost, src_rt_entry->iface);
					fprintf(stderr, "arpcache_sweep: send ICMP DEST_HOST_UNREACHABLE if retry more than 5 times\n");

					break;
				}

				struct cached_pkt *pkt_entry = NULL, *pkt_q;
				list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
					list_delete_entry(&(pkt_entry->list));
					free(pkt_entry->packet); 
					free(pkt_entry);
				}

				list_delete_entry(&(req_entry->list));
				free(req_entry);
			}
			if (req_entry->retries<5 && now - req_entry->sent >= 1) {
				req_entry->sent = now;
				req_entry->retries++;
				arp_send_request(req_entry->iface, req_entry->ip4);
			}
		}

		pthread_mutex_unlock(&arpcache.lock);
		//fprintf(stderr, "sweep arpcache periodically: remove old entries, resend arp requests .\n");
	}

	return NULL;
}
