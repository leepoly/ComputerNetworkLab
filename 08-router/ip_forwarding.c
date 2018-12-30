#include "ip.h"
#include "icmp.h"
#include "rtable.h"
#include "arp.h"
#include "arpcache.h"

#include <stdio.h>
#include <stdlib.h>

// forward the IP packet from the interface specified by longest_prefix_match, 
// when forwarding the packet, you should check the TTL, update the checksum,
// determine the next hop to forward the packet, then send the packet by 
// iface_send_packet_by_arp
void ip_forward_packet(u32 ip_dst, char *packet, int len, iface_info_t *iface)
{
	//lookup route table to find gw_ip and iface
	rt_entry_t* rt_entry_match = longest_prefix_match(ip_dst);
	struct ether_header *eh = (struct ether_header *)packet;
	struct iphdr *ip = packet_to_ip_hdr(packet);
	if (rt_entry_match==NULL) {
		fprintf(stderr, "forward ip packet: no match\n");
		icmp_send_packet((const char *)ip, ip->ihl*4 + 8, 3, 0, iface->ip, ntohl(ip->saddr), eh->ether_shost, eh->ether_dhost, iface);
	} else {
		//whether in the same subnet or not
		//modify IP header: TLL, checksum
		ip->ttl--;
		if (ip->ttl <= 0) {
			fprintf(stderr, "forward ip packet: TTL runs out\n"); 
			icmp_send_packet((const char *)ip, ip->ihl*4 + 8, 11, 0, iface->ip, ntohl(ip->saddr), eh->ether_shost, eh->ether_dhost, iface);
		} else {
			ip->checksum = ip_checksum(ip);
			//send by arp
			u32 next_ip = rt_entry_match->gw==0?ip_dst:rt_entry_match->gw;
			fprintf(stderr, "forward ip packet: send to arp %x \n", next_ip); 
			iface_send_packet_by_arp(rt_entry_match->iface, next_ip, packet, len);
		}
	}
}

// handle ip packet
//
// If the packet is ICMP echo request and the destination IP address is equal to
// the IP address of the iface, send ICMP echo reply; otherwise, forward the
// packet.
void handle_ip_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *ip = packet_to_ip_hdr(packet);
	u32 daddr = ntohl(ip->daddr);
	if (daddr == iface->ip) {
		fprintf(stderr, "handle_ip_packet: reply to the sender if it is ping packet.\n");
		ip_send_packet(packet, len, iface);
		//free(packet);
	}
	else {
		ip_forward_packet(daddr, packet, len, iface);
	}
}
