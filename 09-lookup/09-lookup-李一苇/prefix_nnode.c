#include <stdio.h>
#include <stdlib.h>
#include <sys/timeb.h>
#include <sys/types.h>

#define BUF_SIZE 512
#define BIT_STEP 4 //option: 1, 2, 3, 4
#define DEBUG_FLAG 0
#define RETRY_TIME 10000
#define PROB_TEST 9997 //control the time of testing (not test all ip)
#define CHILD_NUM (1 << BIT_STEP) //CHILD_NUM MAXIMUM: 16

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

typedef struct pt_node {
	int interface_id;
	int mask_len;
#if DEBUG_FLAG==1
	u32 ip_int;
#endif
	struct pt_node* child[CHILD_NUM];
} pt_node; //prefix_tree_node

typedef struct {
	int iface_id;
	int mask_len;
} pt_childleaf_node;

typedef struct {
	struct pt_internal_node* node;
} pt_childinternal_node;

typedef struct pt_internal_node {
#if BIT_STEP == 4
	u16 bitarr;
#elif BIT_STEP == 3
	u8 bitarr;
#else
	unsigned bitarr : CHILD_NUM;
#endif
	pt_childleaf_node* leaf;
	pt_childinternal_node* child;
} pt_internal_node; //compressed vector/pointer tree node

long mem_node_use;
long mem_inode_use;
long mem_cvnode_use;

static inline int min(int x, int y) {
	return (x < y ? x : y);
}

unsigned popcount(u32 u)
{
	u = (u & 0x55555555) + ((u >> 1) & 0x55555555);
	u = (u & 0x33333333) + ((u >> 2) & 0x33333333);
	u = (u & 0x0F0F0F0F) + ((u >> 4) & 0x0F0F0F0F);
	u = (u & 0x00FF00FF) + ((u >> 8) & 0x00FF00FF);
	u = (u & 0x0000FFFF) + ((u >> 16) & 0x0000FFFF);
	return u;
}

long long getSystemTime()
{
	struct timeb t;
	ftime(&t);
	return 1000 * t.time + t.millitm;
}

int is_node_leaf(pt_node *node) {
	for (int i = 0; i < CHILD_NUM; i++) 
		if (node->child[i] != NULL)
			return 0;
	return 1;
}

void init_inode_child(pt_internal_node *node) {
	mem_inode_use += sizeof(pt_childinternal_node) * CHILD_NUM;
	node->child = (pt_childinternal_node*)malloc(sizeof(pt_childinternal_node) * CHILD_NUM);
	for (int i = 0; i < CHILD_NUM; i++) {
		node->child[i].node = NULL;
	}
}

void init_inode_leaf(pt_internal_node *node) {
	mem_inode_use += sizeof(pt_childleaf_node) * CHILD_NUM;
	node->leaf = (pt_childleaf_node*)malloc(sizeof(pt_childleaf_node) * CHILD_NUM);
	for (int i = 0; i < CHILD_NUM; i++) {
		node->leaf[i].iface_id = -1;
		node->leaf[i].mask_len = -1;
	}
}

void init_cvnode(pt_internal_node *node) {
	mem_cvnode_use += sizeof(pt_internal_node);
	node->leaf = NULL;
	node->bitarr = 0;
	node->child = NULL;
}

void init_inode(pt_internal_node *node) {
	mem_inode_use += sizeof(pt_internal_node);
	node->leaf = NULL;
	node->bitarr = 0;
	node->child = NULL;
	init_inode_child(node);
	init_inode_leaf(node);
}

void init_node(pt_node *node) {
	mem_node_use += sizeof(pt_node);
	node->interface_id = -1;
	node->mask_len = -1;
	for (int i = 0; i < CHILD_NUM; i++)
		node->child[i] = NULL;
}

int is_inode_leaf(pt_internal_node *node) {
	return (node->bitarr == 0 ? 1 : 0);
}

int parse_ip_addr(char* str) {
	int vec1, vec2, vec3, vec4;
	sscanf(str, "%d.%d.%d.%d", &vec1, &vec2, &vec3, &vec4);
	return (vec1 << 24) + (vec2 << 16) + (vec3 << 8) + vec4;
}

int get_next_step(u32 ip_int, int depth) {
	u32 mask = 0;
	for (int i = depth; i < min(depth + BIT_STEP, 32); i++) {
		mask |= 1 << (31 - i);
	}
	mask &= ip_int;
	return mask >> (32 - min(depth + BIT_STEP, 32));
}

void find_cvnode(pt_internal_node *node, u32 ip_int, int depth, int *last_match_iface_id) {
	int nx_step = get_next_step(ip_int, depth);
	int is_child_internal = node->bitarr & (1 << (nx_step));
	int prefix_mask = (1 << (nx_step + 1)) - 1; //acquire first nx_step bit of bitarr
	int child_idx = popcount((u32) (node->bitarr & prefix_mask)) - 1; //addr start from zero, -1 means no child yet
	int leaf_idx = nx_step - child_idx - 1; //the same
#if DEBUG_FLAG==1
		printf("\t%d: %d %d %d\n", depth, nx_step, child_idx, leaf_idx);
#endif
	if (is_child_internal > 0) {
		find_cvnode(node->child[child_idx].node, ip_int, depth + BIT_STEP, last_match_iface_id);
	}
	else {
		*last_match_iface_id = node->leaf[leaf_idx].iface_id;
	}
}

void find_node(pt_node *node, u32 ip_int, int depth, int *last_match_iface_id) {
	if (node == NULL) return;
	if (node->interface_id != -1) *last_match_iface_id = node->interface_id;
	int nx_step = get_next_step(ip_int, depth);
	pt_node* nx_node = node->child[nx_step];
	find_node(nx_node, ip_int, depth + BIT_STEP, last_match_iface_id);
}

void find_inode(pt_internal_node *node, u32 ip_int, int depth, int *last_match_iface_id) {
	int nx_step = get_next_step(ip_int, depth);
	int is_child_internal = node->bitarr & (1 << (nx_step));
#if DEBUG_FLAG==1
		printf("\t%d: %d\n", depth, nx_step);
#endif
	if (is_child_internal > 0) {
		find_inode(node->child[nx_step].node, ip_int, depth + BIT_STEP, last_match_iface_id);
	}
	else {
		*last_match_iface_id = node->leaf[nx_step].iface_id;
	}
}

void insert_node(pt_node *node, u32 ip_int, int mask_len, int iface_id, int depth) {
	int nx_step = get_next_step(ip_int, depth);
	pt_node* nx_node = node->child[nx_step];
#if DEBUG_FLAG==1
	printf("\t %d %x %d\n", depth, nx_node, nx_step);
#endif
	if (depth + BIT_STEP < mask_len) {
		if (nx_node == NULL) {
			nx_node = (pt_node*)malloc(sizeof(pt_node));
			init_node(nx_node);
			node->child[nx_step] = nx_node;
		}
		insert_node(nx_node, ip_int, mask_len, iface_id, depth + BIT_STEP);
	}
	else {
		int initial_nx_step = get_next_step(ip_int, depth);
		int until_nx_step = initial_nx_step + (1 << (min(depth + BIT_STEP, 32) - mask_len));
		for (nx_step = initial_nx_step; nx_step < until_nx_step; nx_step++) {
			nx_node = node->child[nx_step];
			if (nx_node == NULL) {
				nx_node = (pt_node*)malloc(sizeof(pt_node));
				init_node(nx_node);
				node->child[nx_step] = nx_node;
			}
#if DEBUG_FLAG==1
			if (nx_node->mask_len == mask_len)
				printf("Impossible: duplicate IP address!\n");
#endif
			if (nx_node->mask_len < mask_len) {
				nx_node->interface_id = iface_id;
				nx_node->mask_len = mask_len;
				#if DEBUG_FLAG==1
				nx_node->ip_int = ip_int; //debug
				#endif
			}
		}
	}
}

void build_tree(pt_node *root) {
	FILE* file;
	if (DEBUG_FLAG)
		file = fopen("forwarding-table1.txt", "r");
	else
		file = fopen("forwarding-table.txt", "r");
	char buf[BUF_SIZE];
	char ip_string[BUF_SIZE];
	int ip_mask;
	int iface_id;
	int cnt = 0;
	while (!feof(file)) {
		//parse input line
		if (fscanf(file, "%s %d %d", ip_string, &ip_mask, &iface_id) < 0) break;
		cnt++;
#if DEBUG_FLAG==1
			printf("%d %s\n", cnt, ip_string); 
#endif
		u32 ip_int = parse_ip_addr(ip_string);
		insert_node(root, ip_int, ip_mask, iface_id, 0);
	}
	fclose(file);
}

void create_pt_internal_tree_from_pt_tree(pt_internal_node *inode, pt_node *node) {
	if (is_node_leaf(node)) return;
	for (int i = 0; i < CHILD_NUM; i++) {
		if (is_node_leaf(node->child[i])) {
			inode->leaf[i].iface_id = node->child[i]->interface_id;
		}
		else {
			inode->bitarr |= 1 << i;
			inode->child[i].node = (pt_internal_node*)malloc(sizeof(pt_internal_node));
			init_inode(inode->child[i].node);
			create_pt_internal_tree_from_pt_tree(inode->child[i].node, node->child[i]);
		}
	}
}

void create_cv_tree_from_internal_tree(pt_internal_node *cvnode, pt_internal_node *inode) {
	cvnode->bitarr = inode->bitarr;
	int ichild_num = popcount((u32) inode->bitarr);
	cvnode->child = (pt_childinternal_node*)malloc(sizeof(pt_childinternal_node) * ichild_num);
	cvnode->leaf = (pt_childleaf_node*)malloc(sizeof(pt_childleaf_node) * (CHILD_NUM - ichild_num));
	mem_cvnode_use += sizeof(pt_childinternal_node) * ichild_num + sizeof(pt_childleaf_node) * (CHILD_NUM - ichild_num);
	int child_idx = 0, leaf_idx = 0;
	for (int i = 0; i < CHILD_NUM; i++) {
		int is_child_node = inode->bitarr & (1 << i);
		if (is_child_node) {
			cvnode->child[child_idx].node = (pt_internal_node*)malloc(sizeof(pt_internal_node));
			init_cvnode(cvnode->child[child_idx].node);
#if DEBUG_FLAG==1
				printf("child: %d %d\n", i, child_idx);
#endif
			create_cv_tree_from_internal_tree(cvnode->child[child_idx].node, inode->child[i].node);
			child_idx++;
		}
		else {
			cvnode->leaf[leaf_idx].iface_id = inode->leaf[i].iface_id;
#if DEBUG_FLAG==1
				printf("leaf: %d %d: %d\n", i, leaf_idx, cvnode->leaf[leaf_idx].iface_id);
#endif
			leaf_idx++;
		}
	}
}

void leaf_pushing(pt_node *node) {
	if (is_node_leaf(node)) return;
	int iface_id = node->interface_id;
	node->interface_id = -1;
	for (int i = 0; i < CHILD_NUM; i++) {
		pt_node *child_node = node->child[i];
		if (child_node == NULL) {
			child_node = (pt_node*)malloc(sizeof(pt_node));
			init_node(child_node);
			node->child[i] = child_node;
		}
		if (child_node->interface_id == -1)
			child_node->interface_id = iface_id;
		leaf_pushing(child_node);
	}
}

void destroy_tree(pt_node *node) {
	if (node == NULL) return;
	for (int i = 0; i < CHILD_NUM; i++) {
		if (node->child[i] != NULL) {
			destroy_tree(node->child[i]);
		}
	}
	free(node);
}

void destroy_itree(pt_internal_node *node) {
	if (node == NULL) return;
	for (int i = 0; i < CHILD_NUM; i++) {
		if (node->child && node->child[i].node != NULL) {
			destroy_itree(node->child[i].node);
		}
	}
	if (node->leaf != NULL) 
		free(node->leaf);
	if (node->child != NULL) 
		free(node->child);
	free(node);
}

void destroy_cvtree(pt_internal_node *node) {
	if (node == NULL) return;
	int child_idx = 0;
	for (int i = 0; i < CHILD_NUM; i++) {
		if (node->bitarr & (1 << i)) {
			if (node->child && node->child[child_idx].node != NULL) {
				destroy_cvtree(node->child[child_idx].node);
			}
			child_idx++;
		}
	}
	if (node->leaf != NULL) 
		free(node->leaf);
	if (node->child != NULL) 
		free(node->child);
	free(node);
}

void output_finding_result(pt_node *root, pt_internal_node *iroot, pt_internal_node *cvroot) {
	if (! DEBUG_FLAG) {
		FILE* file = fopen("result-step0.txt", "r");
		char ip_string[BUF_SIZE];
		int ip_mask;
		int real_iface_id;
		int cnt = 0;
		long long start, end;
		u32 sum_cvtree_time = 0, sum_cptree_time = 0, sum_tree_time = 0;
		while (!feof(file)) {
			//parse input line
			u32 ip_int;
			if (fscanf(file, "%s %x %d", ip_string, &ip_int, &real_iface_id) < 0) break;
			if (!DEBUG_FLAG) { //in real environment, let's do a salty test...
				if ((rand() % 10000) < PROB_TEST) continue;
			}
			printf("%d %s\n", cnt, ip_string);
			cnt++;
			int iface_id = -1;

			start = getSystemTime();
			for (int i = 0; i < RETRY_TIME; i++) {
				find_node(root, ip_int, 0, &iface_id);
			}
			end = getSystemTime();
			sum_tree_time += (end - start);

			start = getSystemTime();
			for (int i = 0; i < RETRY_TIME; i++) {
				find_inode(iroot, ip_int, 0, &iface_id);
			}
			end = getSystemTime();
			sum_cptree_time += (end - start);

			start = getSystemTime();
			for (int i = 0; i < RETRY_TIME; i++) {
				find_cvnode(cvroot, ip_int, 0, &iface_id);
			}
			end = getSystemTime();
			sum_cvtree_time += (end - start);
			
			if (real_iface_id != iface_id) {
				printf("%d %s %x expect: %d got: %d\n", cnt, ip_string, ip_int, real_iface_id, iface_id);
				printf("ERROR!\n");
				break;
			}
		}
		printf("execution time of multiple prefix tree : %d / %d ms.\n", sum_tree_time, (cnt * RETRY_TIME));
		printf("execution time of compressed pointer tree : %d / %d ms.\n", sum_cptree_time, (cnt * RETRY_TIME));
		printf("execution time of compressed vector tree : %d / %d ms.\n", sum_cvtree_time, (cnt * RETRY_TIME));
		printf("memory use of multiple prefix tree : %d\n", mem_node_use);
		printf("memory use of compressed pointer tree : %d\n", mem_inode_use);
		printf("memory use of compressed vector tree : %d\n", mem_cvnode_use);
	}
	else {
		char ip_str[] = "16.0.0.0";
		int iface_id = -1;
		find_cvnode(cvroot, parse_ip_addr(ip_str), 0, &iface_id);
		printf("%x expect: %d got: %d\n", parse_ip_addr(ip_str), 7, iface_id);
	}
}

int main() {
	//build a normal tree
	pt_node *root = (pt_node*)malloc(sizeof(pt_node));
	init_node(root);
	build_tree(root);
	leaf_pushing(root);

	//build a compressed-ptr tree
	pt_internal_node *iroot = (pt_internal_node*)malloc(sizeof(pt_internal_node));
	init_inode(iroot);
	create_pt_internal_tree_from_pt_tree(iroot, root);
	
	//build a compressed-vec tree
	pt_internal_node *cvroot = (pt_internal_node*)malloc(sizeof(pt_internal_node));
	init_cvnode(cvroot);
	create_cv_tree_from_internal_tree(cvroot, iroot);

	//test it
	output_finding_result(root, iroot, cvroot);

	destroy_tree(root);
	destroy_itree(iroot);
	destroy_cvtree(cvroot);
	system("pause");
	return 0;
}
