#include "icmp.h"
#include "ip.h"
#include "rtable.h"
#include "arp.h"
#include "base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void iface_send_packet(iface_info_t*, char*, int);

// send icmp packet
void icmp_send_packet(const char *in_pkt, int len, u8 type, u8 code, u32 src_ip, u32 dst_ip, u8 *src_mac, u8 *dst_mac, iface_info_t *iface)
{
	int icmp_len = 4 + 4 + len;
	char *packet = (char*)malloc(ETHER_HDR_SIZE + 20 + icmp_len);
	struct ether_header *eh = (struct ether_header *)packet;
	struct iphdr *ip = packet_to_ip_hdr(packet);
	struct icmphdr *icmp = (struct icmphdr *)(packet + ETHER_HDR_SIZE + 20); 

	// struct icmphdr *icmp = (struct icmphdr *)(packet + ETHER_HDR_SIZE + 20); 
	// memset(icmp + 4, 0, 4);
	
	//ICMP header+content
	icmp->type = type;
	icmp->code = code;
	memset((char *)icmp + 4, 0, 4);
	memcpy((char *)icmp + 4 + 4, in_pkt, len);
	icmp->checksum = icmp_checksum(icmp, icmp_len);
	//IP hdr
	ip_init_hdr(ip, src_ip, dst_ip, icmp_len + 20, IPPROTO_ICMP);

	//ETHER header
	eh->ether_type = htons(ETH_P_IP);
	memcpy(eh->ether_dhost, src_mac, sizeof(u8)*ETH_ALEN); //switch sender and receiver
	memcpy(eh->ether_shost, dst_mac, sizeof(u8)*ETH_ALEN);

	iface_send_packet(iface, packet, ETHER_HDR_SIZE + 20 + 4 + 4 + len);

	fprintf(stderr, "icmp_send_packet: send an icmp packet. type||code: %d %d\n", type, code);
}
