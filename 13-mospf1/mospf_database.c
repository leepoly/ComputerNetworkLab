#include "mospf_database.h"
#include "ip.h"
#include "rtable.h"
#include "mospf_nbr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

struct list_head mospf_db;

#define MAX_NODE_NUM 255
#define INF 255
int graph[MAX_NODE_NUM][MAX_NODE_NUM];
int visited[MAX_NODE_NUM];
int dist[MAX_NODE_NUM];
int prev[MAX_NODE_NUM];
int node_num;
u32 dic[MAX_NODE_NUM]; //graph node i to 

void init_mospf_db()
{
	init_list_head(&mospf_db);
}

int rid2nodeid(u32 rid) {
	for (int i = 0; i < node_num; i++) {
		if (dic[i] == rid)
			return i;
	}
	return -1;
}

void init_graph() {
	bzero(graph, sizeof(graph));
	bzero(visited, sizeof(visited));

	node_num = 1;
	dic[0] = instance->router_id;
	mospf_db_entry_t *entry = NULL;
	list_for_each_entry(entry, &mospf_db, list) {
		//printf("init_graph: %d -> %o\n", node_num, entry->rid);
		dic[node_num++] = entry->rid;
	}
}

void generate_graph() {
	mospf_db_entry_t *entry = NULL;
	list_for_each_entry(entry, &mospf_db, list) {
		for (int i = 0; i < entry->nadv; i++) {
			if (entry->array[i].rid == 0) continue;
			graph[rid2nodeid(entry->rid)][rid2nodeid(entry->array[i].rid)] = 1;
			graph[rid2nodeid(entry->array[i].rid)][rid2nodeid(entry->rid)] = 1; //must be bidirectional
			//printf("generate_graph: %o <-> %o\n", entry->rid, entry->array[i].rid);
		}
	}
}

void dijkstra() {
	for (int i = 0; i < node_num; i++) {
		dist[i] = INF;
		prev[i] = -1;
	}
	dist[0] = 0;
	for (int i = 0; i < node_num; i++) {
		int min_dist = INF, u = -1;
		for (int j = 0; j < node_num; j++) {
			if (visited[j] == 0 && dist[j] < min_dist) {
				min_dist = dist[j];
				u = j;
			}
		}
		if (u == -1) break;
		visited[u] = 1;
		for (int v = 0; v < node_num; v++) {
			if (visited[v] == 0 && graph[u][v] && dist[u] + graph[u][v] < dist[v]) {
				dist[v] = dist[u] + graph[u][v];
				prev[v] = u;
			}
		}
	}
	printf("dijkstra: shortest path: \n");
	for (int i = 0; i < node_num; i++) {
		printf("dijkstra: %d->%o dist: %d prev: %d\n", i, dic[i], dist[i], prev[i]);
	}
}

int track_path(int dst) {
	while (prev[dst] != 0) {
		dst = prev[dst];
	}
	return dst;
}

rt_entry_t* check_mospf_rib(u32 dst) {
	rt_entry_t *entry = NULL;
	list_for_each_entry(entry, &rtable, list) {
		if ((dst & entry->mask) == (entry->dest & entry->mask)) {
			if (entry->gw != 0) 
				return entry; //found an existed rtable entry and it is generated by us
		}
	}
	return NULL;
}

void clear_mospf_rib() {
	//clear all rib entry generated by us
	rt_entry_t *entry = NULL, *entry_q = NULL;
	list_for_each_entry_safe(entry, entry_q, &rtable, list) {
		if (entry->gw != 0) {
			remove_rt_entry(entry);
		}
	}
}

void update_rib() {
	int min_dist, min_dist_k;
	bzero(visited, sizeof(visited));
	visited[0] = 1;
	clear_mospf_rib();
	while (1) {
		//find node k with shortest link
		min_dist = INF;
		min_dist_k = -1;
		for (int i = 0; i < node_num; i++) {
			if (visited[i] == 0 && dist[i] < min_dist) {
				min_dist = dist[i];
				min_dist_k = i;
			}
		}
		if (min_dist_k == -1) break;
		visited[min_dist_k] = 1;
		
		mospf_db_entry_t *entry = NULL;
		list_for_each_entry(entry, &mospf_db, list) {
			if (entry->rid == dic[min_dist_k]) {
				//find way to this node k: src_iface and dst_ip(gw)
				int next_hop_id = track_path(min_dist_k);
				u32 dst_gw = 0;
				iface_info_t *iface = NULL, *src_iface = NULL;
				int found = 0;
				list_for_each_entry(iface, &instance->iface_list, list) {
					mospf_nbr_t *nbr = NULL;
					list_for_each_entry(nbr, &iface->nbr_list, list) {
						if (nbr->nbr_id == dic[next_hop_id]) {
							dst_gw = nbr->nbr_ip;
							src_iface = iface;
							found = 1;
							break;
						}
					}
					if (found) break;
				}
				if (found == 0) {
					printf("update_rib: can't find the way to rid %o by %o\n", dic[min_dist_k], dic[next_hop_id]);
					//dic[min_dist_k] might break down
					break;
				}
				//put all his subnets into same dst: (iface, gw)
				for (int i = 0; i < entry->nadv; i++) {
					rt_entry_t *new_entry = check_mospf_rib(entry->array[i].subnet);
					if (new_entry) {
						printf("update_rib: entry %o has existed.\n", entry->array[i].subnet); 
						continue;
					}
					new_entry = new_rt_entry(entry->array[i].subnet, entry->array[i].mask, dst_gw, src_iface);
					printf("update_rib: add new entry: %o -> %u %s\n", entry->array[i].subnet, dst_gw, src_iface->name);
					add_rt_entry(new_entry);
				}
				break;
			}
		}
	}
	
}

void generate_rib() {
	init_graph();
	generate_graph();
	dijkstra();
	update_rib();
}