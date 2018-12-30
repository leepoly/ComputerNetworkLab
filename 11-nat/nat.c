#include "nat.h"
#include "ip.h"
#include "icmp.h"
#include "tcp.h"
#include "rtable.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static struct nat_table nat;

void handle_ip_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *ip = packet_to_ip_hdr(packet);
	u32 daddr = ntohl(ip->daddr);
	if (daddr == iface->ip && ip->protocol == IPPROTO_ICMP) {
		struct icmphdr *icmp = (struct icmphdr *)IP_DATA(ip);
		if (icmp->type == ICMP_ECHOREQUEST) {
			icmp_send_packet(packet, len, ICMP_ECHOREPLY, 0);
		}

		free(packet);
	}
	else {
		nat_translate_packet(iface, packet, len);
	}
}

// get the interface from iface name
static iface_info_t *if_name_to_iface(const char *if_name)
{
	iface_info_t *iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		if (strcmp(iface->name, if_name) == 0)
			return iface;
	}

	log(ERROR, "Could not find the desired interface according to if_name '%s'", if_name);
	return NULL;
}

// determine the direction of the packet, DIR_IN / DIR_OUT / DIR_INVALID
static int get_packet_direction(char *packet)
{
	struct iphdr *ip = packet_to_ip_hdr(packet);
	u32 dst = ntohl(ip->daddr);
	u32 src = ntohl(ip->saddr);
	rt_entry_t *dst_entry = longest_prefix_match(dst);
	rt_entry_t *src_entry = longest_prefix_match(src);
	if ((src_entry->iface->ip == nat.internal_iface->ip) && (dst_entry->iface->ip == nat.external_iface->ip)) {
		return DIR_OUT;
	}
	if ((src_entry->iface->ip == nat.external_iface->ip) && (dst == nat.external_iface->ip)) {
		return DIR_IN;
	}
	return DIR_INVALID;
}

struct nat_mapping* nat_table_lookup(u8 srv_hash, u32 ip, u16 port, int dir) {
	struct list_head *head = &nat.nat_mapping_list[srv_hash];
	struct nat_mapping *mapping_entry;
	list_for_each_entry(mapping_entry, head, list) {
		if (dir == DIR_OUT) {
			if (mapping_entry->internal_ip == ip && mapping_entry->internal_port == port) {
				return mapping_entry;
			}
		}
		else {
			if (mapping_entry->external_ip == ip && mapping_entry->external_port == port) {
				return mapping_entry;
			}
		}
	}
	return NULL;
}

u16 assign_external_ports() {
	for (u16 i = NAT_PORT_MIN; i < NAT_PORT_MAX; i++) {
		if (nat.assigned_ports[i] == 0) {
			nat.assigned_ports[i] = 1;
			return i;
		}
	}
	return 0;
}

struct nat_mapping *nat_table_insert(u8 srv_hash, u32 in_ip, u16 in_port, u32 ext_ip, u16 ext_port) {
	struct nat_mapping* entry = (struct nat_mapping *)malloc(sizeof(struct nat_mapping));
	entry->internal_ip = in_ip;
	entry->internal_port = in_port;
	entry->external_ip = ext_ip;
	entry->external_port = ext_port;
	bzero(&(entry->conn), sizeof(struct nat_connection)); 

	pthread_mutex_lock(&nat.lock);
		list_add_tail(&(entry->list), &(nat.nat_mapping_list[srv_hash]));
	pthread_mutex_unlock(&nat.lock);
	return entry;
}

void recover_unused_conn(struct nat_mapping *pos, struct tcphdr *tcp, int dir){
    if(tcp->flags == TCP_FIN + TCP_ACK) {
    	if (dir == DIR_IN)
        	pos->conn.internal_fin = 1;
        else
        	pos->conn.external_fin = 1;
    }

    if(tcp->flags == TCP_RST){
        printf("RST!\n");
        if((pos->conn).internal_fin + (pos->conn).external_fin == 2){
            nat.assigned_ports[pos->external_port] = 0;
            list_delete_entry(&(pos->list));
            free(pos);
        }
    }
}

// do translation for the packet: replace the ip/port, recalculate ip & tcp
// checksum, update the statistics of the tcp connection
void do_translation(iface_info_t *iface, char *packet, int len, int dir)
{
	//assume this is a tcp->ip->ethernet packet
	struct iphdr *ip = packet_to_ip_hdr(packet);
	struct tcphdr *tcp = packet_to_tcp_hdr(packet);
	
	//hash
	char srv_info[4 + 2]; //ip: 4B port: 2B
	u32 srv_ip = dir == DIR_OUT? ntohl(ip->daddr) : ntohl(ip->saddr);
	u16 srv_port = dir == DIR_OUT? ntohs(tcp->dport) : ntohs(tcp->sport);
	memcpy(srv_info, &srv_ip, 4);
	memcpy(srv_info + 4, &srv_port, 2);
	u8 hash_srvinfo = hash8(srv_info, 6);

	//lookup
	struct nat_mapping *entry;
	if (dir == DIR_OUT) {
		entry = nat_table_lookup(hash_srvinfo, ntohl(ip->saddr), ntohs(tcp->sport), dir);

		if (entry == NULL) {
			if (tcp->flags != TCP_SYN) 
				printf("Impossible: first non-recorded packet is not with SYN.\n");
			//append
			u16 ext_port = assign_external_ports();
			if (ext_port == 0) {
				printf("nearly impossible: assignable ports run out.\n");
			}
			entry = nat_table_insert(hash_srvinfo, ntohl(ip->saddr), ntohs(tcp->sport), nat.external_iface->ip, ext_port);
		}
		entry->update_time = time(NULL);
		ip->saddr = htonl(entry->external_ip);
		tcp->sport = htons(entry->external_port);
		tcp->checksum = tcp_checksum(ip, tcp);
    	ip->checksum  = ip_checksum(ip);
	} else {
		entry = nat_table_lookup(hash_srvinfo, ntohl(ip->daddr), ntohs(tcp->dport), dir);
		if (entry == NULL)
			printf("Impossible: receive non-recorded addr info from outside world.\n");
		entry->update_time = time(NULL);
		ip->daddr = htonl(entry->internal_ip);
		tcp->dport = htons(entry->internal_port);
		tcp->checksum = tcp_checksum(ip, tcp);
    	ip->checksum  = ip_checksum(ip);
	}
	recover_unused_conn(entry, tcp, dir);

	//send packet
	ip_send_packet(packet, len);
	//iface_info_t *send_iface = dir == DIR_OUT? nat.external_iface : nat.internal_iface;
	//iface_send_packet(send_iface, packet, len);
	//fprintf(stdout, "TODO: do translation for this packet.\n");
}

void nat_translate_packet(iface_info_t *iface, char *packet, int len)
{
	int dir = get_packet_direction(packet);
	if (dir == DIR_INVALID) {
		log(ERROR, "invalid packet direction, drop it.");
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
		free(packet);
		return ;
	}

	struct iphdr *ip = packet_to_ip_hdr(packet);
	if (ip->protocol != IPPROTO_TCP) {
		log(ERROR, "received non-TCP packet (0x%0hhx), drop it", ip->protocol);
		free(packet);
		return ;
	}

	do_translation(iface, packet, len, dir);
}

// nat timeout thread: find the finished flows, remove them and free port
// resource
void *nat_timeout()
{
	while (1) {
		//fprintf(stdout, "TODO: sweep finished flows periodically.\n");
		pthread_mutex_lock(&nat.lock);
		time_t now = time(NULL);
		for(int i = 0; i < HASH_8BITS; i++){
            struct list_head *head = &nat.nat_mapping_list[i];
			struct nat_mapping *entry, *q;
            list_for_each_entry_safe(entry, q, head, list) {
                if((now - entry->update_time) > TCP_ESTABLISHED_TIMEOUT){
                    printf("Remove aged connections.\n");
                    nat.assigned_ports[entry->external_port] = 0;
                    list_delete_entry(&(entry->list));
                    free(entry);
                }
            }
        }
		pthread_mutex_unlock(&nat.lock);
		sleep(1);
	}

	return NULL;
}

// initialize nat table
void nat_table_init()
{
	memset(&nat, 0, sizeof(nat));

	for (int i = 0; i < HASH_8BITS; i++)
		init_list_head(&nat.nat_mapping_list[i]);

	nat.internal_iface = if_name_to_iface("n1-eth0");
	nat.external_iface = if_name_to_iface("n1-eth1");
	if (!nat.internal_iface || !nat.external_iface) {
		log(ERROR, "Could not find the desired interfaces for nat.");
		exit(1);
	}

	memset(nat.assigned_ports, 0, sizeof(nat.assigned_ports));

	pthread_mutex_init(&nat.lock, NULL);

	pthread_create(&nat.thread, NULL, nat_timeout, NULL);
}

// destroy nat table
void nat_table_destroy()
{
	pthread_mutex_lock(&nat.lock);

	for (int i = 0; i < HASH_8BITS; i++) {
		struct list_head *head = &nat.nat_mapping_list[i];
		struct nat_mapping *mapping_entry, *q;
		list_for_each_entry_safe(mapping_entry, q, head, list) {
			list_delete_entry(&mapping_entry->list);
			free(mapping_entry);
		}
	}

	pthread_kill(nat.thread, SIGTERM);

	pthread_mutex_unlock(&nat.lock);
}
