#include "tcp.h"
#include "tcp_sock.h"
#include "ip.h"
#include "ether.h"

#include "log.h"
#include "list.h"

#include <stdlib.h>
#include <string.h>

// initialize tcp header according to the arguments
static void tcp_init_hdr(struct tcphdr *tcp, u16 sport, u16 dport, u32 seq, u32 ack,
		u8 flags, u16 rwnd)
{
	memset((char *)tcp, 0, TCP_BASE_HDR_SIZE);

	tcp->sport = htons(sport);
	tcp->dport = htons(dport);
	tcp->seq = htonl(seq);
	tcp->ack = htonl(ack);
	tcp->off = TCP_HDR_OFFSET;
	tcp->flags = flags;
	tcp->rwnd = htons(rwnd);
}

void show_snd_buf(struct tcp_sock *tsk){
	struct send_packet *t;
	printf("snd_buf: ");
	list_for_each_entry(t, &tsk->send_buf, list) {
		struct tcphdr *tcp = packet_to_tcp_hdr(t->packet);
		printf("%d->", ntohl(tcp->seq));
	}
	printf("\n");
}

// send a tcp packet
//
// Given that the payload of the tcp packet has been filled, initialize the tcp 
// header and ip header (remember to set the checksum in both header), and emit 
// the packet by calling ip_send_packet.
void tcp_send_packet(struct tcp_sock *tsk, char *packet, int len) 
{
	struct iphdr *ip = packet_to_ip_hdr(packet);
	struct tcphdr *tcp = (struct tcphdr *)((char *)ip + IP_BASE_HDR_SIZE);

	int ip_tot_len = len - ETHER_HDR_SIZE;
	int tcp_data_len = ip_tot_len - IP_BASE_HDR_SIZE - TCP_BASE_HDR_SIZE;

	u32 saddr = tsk->sk_sip;
	u32	daddr = tsk->sk_dip;
	u16 sport = tsk->sk_sport;
	u16 dport = tsk->sk_dport;

	u32 seq = tsk->snd_nxt;
	u32 ack = tsk->rcv_nxt;
	u16 rwnd = tsk->rcv_wnd;

	tcp_init_hdr(tcp, sport, dport, seq, ack, TCP_PSH|TCP_ACK, rwnd);
	ip_init_hdr(ip, saddr, daddr, ip_tot_len, IPPROTO_TCP); 

	tcp->checksum = tcp_checksum(ip, tcp);

	ip->checksum = ip_checksum(ip);

	tsk->snd_nxt += tcp_data_len;
	tsk->snd_wnd -= tcp_data_len;

	struct send_packet *send_pkt = (struct send_packet *)malloc(sizeof(struct send_packet));
	send_pkt->packet = (char *)malloc(len);
	send_pkt->len = len;
	send_pkt->retrans_num = 0;
	send_pkt->seq = ntohl(tcp->seq);
	memcpy(send_pkt->packet, packet, len);
	list_add_tail(&send_pkt->list, &tsk->send_buf);
	tcp_set_retrans_timer(tsk);
	printf("debug sent tcp packet seq:%d ack:%d\n", ntohl(tcp->seq), ntohl(tcp->ack));
	show_snd_buf(tsk);

	ip_send_packet(packet, len);
}


// send a tcp control packet
//
// The control packet is like TCP_ACK, TCP_SYN, TCP_FIN (excluding TCP_RST).
// All these packets do not have payload and the only difference among these is 
// the flags.
void tcp_send_control_packet(struct tcp_sock *tsk, u8 flags)
{
	int pkt_size = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + TCP_BASE_HDR_SIZE;
	char *packet = malloc(pkt_size);
	if (!packet) {
		log(ERROR, "malloc tcp control packet failed.");
		return ;
	}

	struct iphdr *ip = packet_to_ip_hdr(packet);
	struct tcphdr *tcp = (struct tcphdr *)((char *)ip + IP_BASE_HDR_SIZE);

	u16 tot_len = IP_BASE_HDR_SIZE + TCP_BASE_HDR_SIZE;

	ip_init_hdr(ip, tsk->sk_sip, tsk->sk_dip, tot_len, IPPROTO_TCP);
	tcp_init_hdr(tcp, tsk->sk_sport, tsk->sk_dport, tsk->snd_nxt, \
			tsk->rcv_nxt, flags, tsk->rcv_wnd);

	tcp->checksum = tcp_checksum(ip, tcp);

	if (flags & (TCP_SYN|TCP_FIN | TCP_ACK)) {
		printf("debug sent tcp packet(ctr) seq:%d ack:%d flags:%d\n", ntohl(tcp->seq), ntohl(tcp->ack), tcp->flags);
	}

	if (flags & (TCP_SYN|TCP_FIN)) {
		tsk->snd_nxt += 1;

		struct send_packet *send_pkt = (struct send_packet *)malloc(sizeof(struct send_packet));
		send_pkt->packet = (char *)malloc(pkt_size);
		send_pkt->len = pkt_size;
		send_pkt->retrans_num = 0;
		send_pkt->seq = ntohl(tcp->seq);
		memcpy(send_pkt->packet, packet, pkt_size);
		list_add_tail(&send_pkt->list, &tsk->send_buf);
		tcp_set_retrans_timer(tsk);
		show_snd_buf(tsk);
	}
	ip_send_packet(packet, pkt_size);
}

// send tcp reset packet
//
// Different from tcp_send_control_packet, the fields of reset packet is 
// from tcp_cb instead of tcp_sock.
void tcp_send_reset(struct tcp_cb *cb)
{
	int pkt_size = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + TCP_BASE_HDR_SIZE;
	char *packet = malloc(pkt_size);
	if (!packet) {
		log(ERROR, "malloc tcp control packet failed.");
		return ;
	}

	struct iphdr *ip = packet_to_ip_hdr(packet);
	struct tcphdr *tcp = (struct tcphdr *)((char *)ip + IP_BASE_HDR_SIZE);

	u16 tot_len = IP_BASE_HDR_SIZE + TCP_BASE_HDR_SIZE;
	ip_init_hdr(ip, cb->daddr, cb->saddr, tot_len, IPPROTO_TCP);
	tcp_init_hdr(tcp, cb->dport, cb->sport, 0, cb->seq_end, TCP_RST|TCP_ACK, 0);
	tcp->checksum = tcp_checksum(ip, tcp);

	ip_send_packet(packet, pkt_size);
}
