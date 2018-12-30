#include "mospf_daemon.h"
#include "mospf_proto.h"
#include "mospf_nbr.h"
#include "mospf_database.h"

#include "ip.h"
#include "packet.h"
#include "rtable.h"

#include "list.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

extern ustack_t *instance;

pthread_mutex_t mospf_lock;

static char mospf_hello_dst_mac[6] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0x05};

void mospf_send_hello(u32 rid, u32 mask, u8 *src_mac, u32 src_ip, iface_info_t* iface) {
	int packet_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE;
	char *packet = (char *)malloc(packet_len);
	struct ether_header *ether = (struct ether_header *)packet;
	memcpy(ether->ether_dhost, mospf_hello_dst_mac, ETH_ALEN);
	memcpy(ether->ether_shost, src_mac, ETH_ALEN);
	ether->ether_type = htons(ETH_P_IP);

	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	ip_init_hdr(ip, src_ip, MOSPF_ALLSPFRouters, IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE, IPPROTO_MOSPF);

	struct mospf_hdr *mospf = (struct mospf_hdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
	mospf_init_hdr(mospf, MOSPF_TYPE_HELLO, MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE, rid, instance->area_id);

	struct mospf_hello *hello = (struct mospf_hello *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE);
	mospf_init_hello(hello, mask);
	mospf->checksum = mospf_checksum(mospf);

	iface_send_packet(iface, packet, packet_len);
}

void mospf_send_lsu() {
	//1. summarize all neighbors info
	//pthread_mutex_lock(&mospf_lock);
	//fprintf(stdout, "send_lsu: start summarize\n");
		int num_nbr = 0;
		iface_info_t *iface = NULL;
		list_for_each_entry(iface, &instance->iface_list, list) {
			if (iface->num_nbr > 0) num_nbr += iface->num_nbr;
			else num_nbr++; //add an empty lsa(mark the iface itself)
		}
		struct mospf_lsa nbr_info[num_nbr];
		int nbr_info_idx = 0;
		list_for_each_entry(iface, &instance->iface_list, list) {
			mospf_nbr_t *nbr = NULL;
			int non_nbr = 1;
			list_for_each_entry(nbr, &iface->nbr_list, list) {
				nbr_info[nbr_info_idx].subnet = htonl(nbr->nbr_ip & nbr->nbr_mask);
				nbr_info[nbr_info_idx].mask = htonl(nbr->nbr_mask);
				nbr_info[nbr_info_idx].rid = htonl(nbr->nbr_id);
				nbr_info_idx++;
				non_nbr = 0;
			}
			if (non_nbr) { //add an empty lsa(mark the iface itself)
				fprintf(stdout, "Empty Nbr Iface"IP_FMT"  "IP_FMT"\n",
		               HOST_IP_FMT_STR(iface->ip),
		               HOST_IP_FMT_STR(iface->mask));
				nbr_info[nbr_info_idx].subnet = htonl(iface->ip & iface->mask);
				nbr_info[nbr_info_idx].mask = htonl(iface->mask);
				nbr_info[nbr_info_idx].rid = htonl(0); //set neighbor rid to 0
				nbr_info_idx++;
			}
		}
	//pthread_mutex_unlock(&mospf_lock); //if num_nbr changes, there might be stack overflow
	//2. generate LSU packet
	instance->sequence_num++; //QUESTION
	int packet_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + num_nbr * sizeof(struct mospf_lsa);
	//fprintf(stdout, "send_lsu: total %d\n", num_nbr);
	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		mospf_nbr_t *nbr = NULL;
		list_for_each_entry(nbr, &iface->nbr_list, list) {
			char *packet = (char *)malloc(packet_len);
			//reserve ether hdr but no need to fill, will be done in send_ip_packet_by_arp()
			struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
			ip_init_hdr(ip, iface->ip, nbr->nbr_ip, packet_len - ETHER_HDR_SIZE, IPPROTO_MOSPF);

			struct mospf_hdr *mospf = (struct mospf_hdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
			mospf_init_hdr(mospf, MOSPF_TYPE_LSU, packet_len - ETHER_HDR_SIZE - IP_BASE_HDR_SIZE, instance->router_id, instance->area_id);

			struct mospf_lsu *lsu = (struct mospf_lsu *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE);
			mospf_init_lsu(lsu, num_nbr);

			memcpy(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE, nbr_info, num_nbr * sizeof(struct mospf_lsa));

			mospf->checksum = mospf_checksum(mospf);
			//3. forward to each neighbor
			ip_send_packet(packet, packet_len);
		}
	}
}

void mospf_init()
{
	pthread_mutex_init(&mospf_lock, NULL);

	instance->area_id = 0;
	// get the ip address of the first interface
	iface_info_t *iface = list_entry(instance->iface_list.next, iface_info_t, list);
	instance->router_id = iface->ip;
	instance->sequence_num = 0;
	instance->lsuint = MOSPF_DEFAULT_LSUINT;

	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		iface->helloint = MOSPF_DEFAULT_HELLOINT;
		init_list_head(&iface->nbr_list);
	}

	init_mospf_db();
}

void *sending_mospf_hello_thread(void *param);
void *sending_mospf_lsu_thread(void *param);
void *checking_nbr_and_db_thread(void *param);
void *generating_rib_from_mospf_db(void *param);

void mospf_run()
{
	pthread_t hello, lsu, nbr, db_rib;
	pthread_create(&hello, NULL, sending_mospf_hello_thread, NULL);
	pthread_create(&lsu, NULL, sending_mospf_lsu_thread, NULL);
	pthread_create(&nbr, NULL, checking_nbr_and_db_thread, NULL);
	pthread_create(&db_rib, NULL, generating_rib_from_mospf_db, NULL);
}

void *sending_mospf_hello_thread(void *param)
{
	while (1) {
		iface_info_t *iface = NULL;
		list_for_each_entry(iface, &instance->iface_list, list) {
			mospf_send_hello(instance->router_id, iface->mask, iface->mac, iface->ip, iface);
			//fprintf(stdout, "TODO: send mOSPF Hello message periodically. %s\n", iface->name);
		}
		sleep(MOSPF_DEFAULT_HELLOINT); //because every iface should sleep 5s
	}
	return NULL;
}

void mospf_db_genearating_rib() {
	// printf("---dumping **nbr** of this instance---\n");
	// int num_nbr = 0;
	// struct mospf_lsa nbr_info;
	// iface_info_t *iface = NULL;
	// list_for_each_entry(iface, &instance->iface_list, list) {
	// 	num_nbr += iface->num_nbr;
	// }
	// printf("%d\n", num_nbr);
	// list_for_each_entry(iface, &instance->iface_list, list) {
	// 	mospf_nbr_t *nbr = NULL;
	// 	list_for_each_entry(nbr, &iface->nbr_list, list) {
	// 		nbr_info.subnet = (nbr->nbr_ip);
	// 		nbr_info.mask = (nbr->nbr_mask);
	// 		nbr_info.rid = (nbr->nbr_id);
	// 		printf(IP_FMT"  "IP_FMT"  "IP_FMT"\n",
 //                   HOST_IP_FMT_STR(nbr_info.subnet),
 //                   HOST_IP_FMT_STR(nbr_info.mask),
 //                   HOST_IP_FMT_STR(nbr_info.rid));
	// 	}
	// }
	// printf("---end---\n");
	printf("---dumping **database** of this instance---\n");
	mospf_db_entry_t *entry = NULL;
	list_for_each_entry(entry, &mospf_db, list) {
		for (int i = 0; i < entry->nadv; i++) {
			fprintf(stdout, IP_FMT"  "IP_FMT"  "IP_FMT"  "IP_FMT"\n",
               HOST_IP_FMT_STR(entry->rid),
               HOST_IP_FMT_STR(entry->array[i].subnet),
               HOST_IP_FMT_STR(entry->array[i].mask),
               HOST_IP_FMT_STR(entry->array[i].rid));
		}
	}
	printf("---end---\n");
	generate_rib();
	print_rtable();
}

void *generating_rib_from_mospf_db(void *param)
{
	//fprintf(stdout, "TODO: neighbor list timeout operation.\n");
	while (1) {
		mospf_db_genearating_rib();
		sleep(5);
	}
	return NULL;
}

void *checking_nbr_and_db_thread(void *param)
{
	//fprintf(stdout, "TODO: neighbor list timeout operation.\n");
	while (1) {
		pthread_mutex_lock(&mospf_lock);
			iface_info_t *iface = NULL;
			list_for_each_entry(iface, &instance->iface_list, list) {
				mospf_nbr_t *nbr = NULL, *nbr_q = NULL;
				list_for_each_entry_safe(nbr, nbr_q, &iface->nbr_list, list) {
					nbr->alive++;
					if (nbr->alive > MOSPF_NEIGHBOR_TIMEOUT) {
						list_delete_entry(&(nbr->list));
						//TODO: need free memory
                        iface->num_nbr--;
                        mospf_send_lsu();
					}
				}
			}

			mospf_db_entry_t *entry = NULL, *entry_q= NULL;
			list_for_each_entry_safe(entry, entry_q, &mospf_db, list) {
				entry->alive++;
				if (entry->alive > MOSPF_DATABASE_ENTRY_TIMEOUT) {
					list_delete_entry(&(entry->list));
					//TODO: need free memory
				}
			}
		pthread_mutex_unlock(&mospf_lock);
		sleep(1);
	}
	return NULL;
}

void *sending_mospf_lsu_thread(void *param)
{
	while (1) {
		mospf_send_lsu();
		fprintf(stdout, "send mOSPF LSU message periodically.\n");
		sleep(instance->lsuint);
	}
	return NULL;
}

void handle_mospf_hello(iface_info_t *iface, const char *packet, int len)
{
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
	struct mospf_hello *hello = (struct mospf_hello *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE);
	mospf_nbr_t *nbr = NULL;
	int flag = 0;
	pthread_mutex_lock(&mospf_lock);
		list_for_each_entry(nbr, &iface->nbr_list, list) {
			if (ntohl(mospf->rid) == nbr->nbr_id) {
				nbr->alive = 0;
				flag = 1;
				break;
			}
		}
		if (flag == 0){
			iface->num_nbr++;
			mospf_nbr_t *new_nbr = (mospf_nbr_t *)malloc(sizeof(mospf_nbr_t));
			new_nbr->nbr_id = ntohl(mospf->rid);
			new_nbr->nbr_ip = ntohl(ip->saddr);
			new_nbr->nbr_mask = ntohl(hello->mask);
			new_nbr->alive = 0;
			list_add_tail(&(new_nbr->list), &(iface->nbr_list));
			pthread_mutex_unlock(&mospf_lock);
			fprintf(stdout, "handle_hello: add new nbr\n");
			mospf_send_lsu();
		}
		else 
			pthread_mutex_unlock(&mospf_lock);
}

void handle_mospf_lsu(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
	struct mospf_lsu *lsu = (struct mospf_lsu *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE);
	struct mospf_lsa *lsa = (struct mospf_lsa *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE);
	pthread_mutex_lock(&mospf_lock);
		int found = 0; int updated = 0;
		mospf_db_entry_t *entry = NULL;
		list_for_each_entry(entry, &mospf_db, list) {
			if (entry->rid == ntohl(mospf->rid)) {
				found = 1;
				entry->alive = 0;
				if (entry->seq < ntohs(lsu->seq)) {
					entry->seq = ntohs(lsu->seq);
					entry->nadv = ntohl(lsu->nadv);
					for (int i = 0; i < entry->nadv; i++) {
						entry->array[i].subnet = ntohl(lsa[i].subnet);
						entry->array[i].mask = ntohl(lsa[i].mask);
						entry->array[i].rid = ntohl(lsa[i].rid);
						fprintf(stdout, "Update existed db entry "IP_FMT"  "IP_FMT"  "IP_FMT"  "IP_FMT"\n",
				               HOST_IP_FMT_STR(entry->rid),
				               HOST_IP_FMT_STR(entry->array[i].subnet),
				               HOST_IP_FMT_STR(entry->array[i].mask),
				               HOST_IP_FMT_STR(entry->array[i].rid));
					}
					updated = 1;
				}
				break;
			}
		}
		if (found == 0) {
			mospf_db_entry_t *new_entry = (mospf_db_entry_t *)malloc(sizeof(mospf_db_entry_t));
			new_entry->alive = 0;
			new_entry->rid = ntohl(mospf->rid);
			new_entry->seq = ntohs(lsu->seq);
			new_entry->nadv = ntohl(lsu->nadv);
			new_entry->array = (struct mospf_lsa*)malloc(new_entry->nadv * sizeof(struct mospf_lsa));
			for (int i = 0; i < new_entry->nadv; i++) {
				new_entry->array[i].subnet = ntohl(lsa[i].subnet);
				new_entry->array[i].mask = ntohl(lsa[i].mask);
				new_entry->array[i].rid = ntohl(lsa[i].rid);
				fprintf(stdout, "Add new db entry "IP_FMT"  "IP_FMT"  "IP_FMT"  "IP_FMT"\n",
		               HOST_IP_FMT_STR(new_entry->rid),
		               HOST_IP_FMT_STR(new_entry->array[i].subnet),
		               HOST_IP_FMT_STR(new_entry->array[i].mask),
		               HOST_IP_FMT_STR(new_entry->array[i].rid));
			}
			updated = 1;
			list_add_tail(&new_entry->list, &mospf_db);
		}
	pthread_mutex_unlock(&mospf_lock);
	//forward this LSU packet
	//QUESTION: if (updated > 0)
	if (updated > 0) {
		if (--lsu->ttl > 0) {
			mospf->checksum = mospf_checksum(mospf); //because we modified ttl
			iface_info_t *iface = NULL;
			list_for_each_entry(iface, &instance->iface_list, list) {
				mospf_nbr_t *nbr = NULL;
				list_for_each_entry(nbr, &iface->nbr_list, list) {
					if (nbr->nbr_ip == ntohl(ip->saddr)) continue;
					if (nbr->nbr_id == ntohl(mospf->rid)) continue;
					char *new_packet = (char *)malloc(len);
					memcpy(new_packet, packet, len);
					struct iphdr *new_ip = (struct iphdr *)(new_packet + ETHER_HDR_SIZE);
					new_ip->saddr = htonl(iface->ip);
					new_ip->daddr = htonl(nbr->nbr_ip);
					new_ip->checksum = ip_checksum(new_ip);
					fprintf(stdout, "handle_lsu: forwarding from %o to %o\n", iface->ip, nbr->nbr_ip);
					ip_send_packet(new_packet, len);
				}
			}
		}
	}
	//fprintf(stdout, "TODO: handle mOSPF LSU message.\n");
}

void handle_mospf_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + iphdr_SIZE(ip));

	if (mospf->version != MOSPF_VERSION) {
		log(ERROR, "received mospf packet with incorrect version (%d)", mospf->version);
		return ;
	}
	if (mospf->checksum != mospf_checksum(mospf)) {
		log(ERROR, "received mospf packet with incorrect checksum");
		return ;
	}
	if (ntohl(mospf->aid) != instance->area_id) {
		log(ERROR, "received mospf packet with incorrect area id");
		return ;
	}

	// log(DEBUG, "received mospf packet, type: %d", mospf->type);

	switch (mospf->type) {
		case MOSPF_TYPE_HELLO:
			handle_mospf_hello(iface, packet, len);
			break;
		case MOSPF_TYPE_LSU:
			handle_mospf_lsu(iface, packet, len);
			break;
		default:
			log(ERROR, "received mospf packet with unknown type (%d).", mospf->type);
			break;
	}
}
