#include "arp.h"
#include "base.h"
#include "types.h"
#include "packet.h"
#include "ether.h"
#include "arpcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

// send an arp request: encapsulate an arp request packet, send it out through
// iface_send_packet
extern void refreshARPCacheEntry(u32, u8*);

void arp_init_hdr(struct ether_arp *arp, u16 op, u8* sha, u32 spa, u8* tha, u32 tpa)
{
	arp->arp_hrd = htons(ARPHRD_ETHER);
	arp->arp_pro = htons(0x0800);
	arp->arp_hln = 6;
	arp->arp_pln = 4;
	arp->arp_op = htons(op);
	memcpy(arp->arp_sha, sha, sizeof(u8)*ETH_ALEN);
	arp->arp_spa = htonl(spa);
	memcpy(arp->arp_tha, tha, sizeof(u8)*ETH_ALEN);
	arp->arp_tpa = htonl(tpa);
}

void arp_send_request(iface_info_t *iface, u32 dst_ip)
{
	fprintf(stderr, "send arp request when lookup failed in arpcache.\n");
	char * packet = (char *)malloc(sizeof(struct ether_header) + sizeof(struct ether_arp));
	struct ether_header *eh = (struct ether_header *)packet;
	struct ether_arp *arp = (struct ether_arp*)packet_to_arp_hdr(packet);
	for (int i = 0; i < ETH_ALEN; i++)
		eh->ether_dhost[i] = 0xFF;
	memcpy(eh->ether_shost, iface->mac, sizeof(u8)*ETH_ALEN);
	eh->ether_type = htons(ETH_P_ARP);

	arp_init_hdr(arp, ARPOP_REQUEST, iface->mac, iface->ip, eh->ether_dhost, dst_ip);

	int len = sizeof(struct ether_header) + sizeof(struct ether_arp);
	iface_send_packet(iface, packet, len);
}

// send an arp reply packet: encapsulate an arp reply packet, send it out
// through iface_send_packet
void arp_send_reply(iface_info_t *iface, char *packet, int len)
{
	struct ether_header *eh = (struct ether_header *)packet;
	struct ether_arp* arp = (struct ether_arp*)packet_to_arp_hdr(packet);
	memcpy(eh->ether_dhost, arp->arp_tha, sizeof(u8)*ETH_ALEN);
	memcpy(eh->ether_shost, arp->arp_sha, sizeof(u8)*ETH_ALEN);
	iface_send_packet(iface, packet, len);
	fprintf(stderr, "send arp reply when receiving arp request.\n");
}

void handle_arp_packet(iface_info_t *iface, char *packet, int len)
{
	struct ether_arp* arp = (struct ether_arp*)packet_to_arp_hdr(packet);
	if (ntohs(arp->arp_op) == ARPOP_REQUEST) { //receive a request
		if (ntohl(arp->arp_tpa) == iface->ip) {
			fprintf(stderr, "handle_arp_packet: receive arp request and request me.\n");
			//request my MAC Addr
			u8 req_src_mac[ETH_ALEN];
			u32 req_src_ip = arp->arp_spa;
			memcpy(req_src_mac, arp->arp_sha, sizeof(u8)*ETH_ALEN);

			arp->arp_op = htons(ARPOP_REPLY);
			memcpy(arp->arp_sha, iface->mac, sizeof(u8)*ETH_ALEN);
			arp->arp_spa = htonl(iface->ip);
			memcpy(arp->arp_tha, req_src_mac, sizeof(u8)*ETH_ALEN);
			arp->arp_tpa = req_src_ip;

			arp_send_reply(iface, packet, len);
		} else {
			//not request my MAC but if my ARP cache got arp_tpa's entry, need refresh
			refreshARPCacheEntry(ntohl(arp->arp_spa), arp->arp_sha);
		}
	} else if (ntohs(arp->arp_op) == ARPOP_REPLY) { //receive a reply
		fprintf(stderr, "handle_arp_packet: receive arp reply.\n");
		if (ntohl(arp->arp_tpa) == iface->ip) {
			arpcache_insert(ntohl(arp->arp_spa), arp->arp_sha);
		} else {
			fprintf(stderr, "handle_arp_packet: impossible! I received a reply arp packet but I'm not the req host.\n");
		}

	}
}

// send (IP) packet through arpcache lookup 
//
// Lookup the mac address of dst_ip in arpcache. If it is found, fill the
// ethernet header and emit the packet by iface_send_packet, otherwise, pending 
// this packet into arpcache, and send arp request.
void iface_send_packet_by_arp(iface_info_t *iface, u32 dst_ip, char *packet, int len)
{
	struct ether_header *eh = (struct ether_header *)packet;
	eh->ether_type = htons(ETH_P_IP);

	u8 dst_mac[ETH_ALEN];
	int found = arpcache_lookup(dst_ip, dst_mac);
	if (found) {
		log(DEBUG, "found the mac of %x, send this packet", dst_ip);
		memcpy(eh->ether_shost, iface->mac, sizeof(u8) * ETH_ALEN);
		memcpy(eh->ether_dhost, dst_mac, sizeof(u8) * ETH_ALEN);
		iface_send_packet(iface, packet, len);
	}
	else {
		log(DEBUG, "lookup %x failed, pend this packet", dst_ip);
		arpcache_append_packet(iface, dst_ip, packet, len);
	}
}
