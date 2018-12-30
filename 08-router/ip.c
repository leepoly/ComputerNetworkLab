#include "ip.h"
#include "icmp.h"
#include "packet.h"
#include "arpcache.h"
#include "rtable.h"
#include "arp.h"

// #include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// initialize ip header 
void ip_init_hdr(struct iphdr *ip, u32 saddr, u32 daddr, u16 len, u8 proto)
{
	ip->version = 4;
	ip->ihl = 5;
	ip->tos = 0;
	ip->tot_len = htons(len);
	ip->id = rand();
	ip->frag_off = htons(IP_DF);
	ip->ttl = DEFAULT_TTL;
	ip->protocol = proto;
	ip->saddr = htonl(saddr);
	ip->daddr = htonl(daddr);
	ip->checksum = ip_checksum(ip);
}

// lookup in the routing table, to find the entry with the same and longest prefix.
// the input address is in host byte order
rt_entry_t *longest_prefix_match(u32 dst)
{
	rt_entry_t *entry = NULL;
	rt_entry_t *ret_entry = NULL;
	list_for_each_entry(entry, &rtable, list) {
		if (((dst&entry->mask) == (entry->dest&entry->mask)))
			if ((!ret_entry) || (entry->mask > ret_entry->mask)) {
				ret_entry = entry;
				fprintf(stderr, "longest_prefix_match: longest prefix match for the packet.\n");
			}
	}

	return ret_entry;
}

// send IP packet
//
// Different from ip_forward_packet, ip_send_packet sends packet generated by
// router itself. This function is used to send ICMP packets.
void ip_send_packet(char *packet, int len, iface_info_t *iface)
{
	struct ether_header *eh = (struct ether_header *)packet;
	struct iphdr *ip = packet_to_ip_hdr(packet);
	struct icmphdr *icmp = (struct icmphdr *)(packet + ETHER_HDR_SIZE + 20); 
	if (icmp->type != 8 || icmp->code != 0) {
		fprintf(stderr, "ip_send_packet: Impossible! A router received non-ping icmp.\n");
	}

	//modify ICMP header+content
	int icmp_len = len - ETHER_HDR_SIZE - ip->ihl * 4;
	icmp->type = 0;
	icmp_checksum(icmp, icmp_len);

	//modify IP header
	ip_init_hdr(ip, ntohl(ip->daddr), ntohl(ip->saddr), ntohs(ip->tot_len), ip->protocol);

	//modify ETHER header
	u8 tmp[ETH_ALEN];
	memcpy(tmp, eh->ether_dhost, sizeof(u8)*ETH_ALEN);
	memcpy(eh->ether_dhost, eh->ether_shost, sizeof(u8)*ETH_ALEN);
	memcpy(eh->ether_shost, tmp, sizeof(u8)*ETH_ALEN);

	iface_send_packet(iface, packet, len);

	fprintf(stderr, "ip_send_packet: send ip packet.\n");
}
