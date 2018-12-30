#include "mospf_database.h"
#include "ip.h"

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

struct list_head mospf_db;

#define MAX_NODE_NUM 255
#define INF 255
int graph[MAX_NODE_NUM][MAX_NODE_NUM];
int visited[MAX_NODE_NUM];
int dist[MAX_NODE_NUM];
int prev[MAX_NODE_NUM];
int node_num;
u32 dic[MAX_NODE_NUM]; //router_id to graph node i

void init_mospf_db()
{
	init_list_head(&mospf_db);
}

// int rid2nodeid(u32 rid) {
// 	for (int i = 0; i < node_num; i++) {
// 		if (dic[i] == rid)
// 			return i;
// 	}
// 	return -1;
// }

// void init_graph() {
// 	bzero(graph, sizeof(graph));
// 	bzero(visited, sizeof(visited));
// 	memset(dist, INF, sizeof(dist));
// 	memset(prev, -1, sizeof(dist));

// 	node_num = 1;
// 	dic[0] = instance->router_id;
// 	mospf_db_entry_t *entry = NULL;
// 	list_for_each_entry(entry, &mospf_db, list) {
// 		dic[node_num++] = entry->rid;
// 	}
// }

// void generate_graph() {
// 	mospf_db_entry_t *entry = NULL;
// 	list_for_each_entry(entry, &mospf_db, list) {
// 		for (int i = 0; i < entry->nadv; i++) {
// 			if (entry->array[i].rid == 0) continue;
// 			graph[rid2nodeid(entry->rid), rid2nodeid(entry->array[i].rid)] = 1;
// 			graph[rid2nodeid(entry->array[i].rid), rid2nodeid(entry->rid)] = 1; //must be bidirectional
// 		}
// 	}
// }

// void dijkstra() {
// 	dist[0] = 0;
// 	for (int i = 0; i < node_num; i++) {
// 		int min_dist = INF, u = -1;
// 		for (int j = 0; j < node_num; j++) {
// 			if (visited[j] == 0 && dist[j] < min_dist) {
// 				min_dist = dist[j];
// 				u = j;
// 			}
// 		}
// 		if (u == -1) break;
// 		visited[u] = true;
// 		for (int v = 0; v < node_num; v++) {
// 			if (visited[v] == 0 && graph[u][v] && dist[u] + graph[u][v] < dist[v]) {
// 				dist[v] = dist[u] + graph[u][v];
// 				prev[v] = u;
// 			}
// 		}
// 	}
// }

// void update_rib() {
	
// }

// void generate_rib() {
// 	init_graph();
// 	generate_graph();
// 	dijkstra();
// 	update_rib();
// }