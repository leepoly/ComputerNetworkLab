#include <stdio.h>
#include <stdlib.h>

#define CHILD_NUM 2
#define BUF_SIZE 512

typedef unsigned int u32;

typedef struct pt_node {
	int interface_id;
	int mask_len;
	struct pt_node* child[CHILD_NUM];
} pt_node; //prefix_tree_node

int cnt = 0;

int index_of_str(char* str, char subch, int startpos) {
	int pos = startpos;
	while (str[pos] != '\0') {
		pos++;
		if (str[pos] == subch) return pos;
	}
	return -1;
}

void init_node(pt_node *node) {
	node->interface_id = -1;
	node->mask_len = 0;
	for (int i = 0; i < CHILD_NUM; i++)
		node->child[i] = NULL;
}

int parse_ip_addr(char* str) {
	int vec1, vec2, vec3, vec4;
	sscanf(str, "%d.%d.%d.%d", &vec1, &vec2, &vec3, &vec4);
	return (vec1 << 24) + (vec2 << 16) + (vec3 << 8) + vec4;
}

int is_node_leaf(pt_node *node) {
	for (int i = 0; i < CHILD_NUM; i++) {
		if (node->child[i] != NULL)
			return 0;
	}
	return 1;
}

void find_node(pt_node *node, u32 ip_int, int depth, int *last_match_iface_id) {
	if (node == NULL) return;
	if (node->interface_id != -1) *last_match_iface_id = node->interface_id;
	int nx_step = (ip_int & (1 << (31 - depth))) > 0 ? 1 : 0;
	//printf("find\t%d: %d\n", depth, nx_step);
	pt_node* nx_node = node->child[nx_step];

	find_node(nx_node, ip_int, depth + 1, last_match_iface_id);
}

void insert_node(pt_node *node, u32 ip_int, int mask_len, int iface_id, int depth) {
	int nx_step = (ip_int & (1 << (31 - depth))) > 0 ? 1 : 0;
	pt_node* nx_node = node->child[nx_step];
	
	if (nx_node == NULL) {
		nx_node = (pt_node*)malloc(sizeof(pt_node));
		init_node(nx_node);
		node->child[nx_step] = nx_node;
	}

	if (depth + 1 == mask_len) {
		nx_node->interface_id = iface_id;
		return;
	}
	//printf("\t%d: %d %x\n", depth, nx_step, nx_node);
	insert_node(nx_node, ip_int, mask_len, iface_id, depth + 1);
}

void build_tree(pt_node *root) {
	FILE* file = fopen("forwarding-table.txt", "r");
	char buf[BUF_SIZE];
	char ip_string[BUF_SIZE];
	int ip_mask;
	int iface_id;
	while (!feof(file)) {
		//parse input line
		if (fscanf(file, "%s %d %d", ip_string, &ip_mask, &iface_id) < 0) break;
		
		cnt++;
		//printf("%d %s\n", cnt, ip_string);
		u32 ip_int = parse_ip_addr(ip_string);
		insert_node(root, ip_int, ip_mask, iface_id, 0);
	}

	fclose(file);
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

void output_finding_result(pt_node *root) {
	FILE* file = fopen("forwarding-table.txt", "r");
	FILE* o_file = fopen("result-step0.txt", "w+");
	char ip_string[BUF_SIZE];
	int ip_mask;
	int real_iface_id;
	while (!feof(file)) {
		//parse input line
		if (fscanf(file, "%s %d %d", ip_string, &ip_mask, &real_iface_id) < 0) break;
		//printf("%d %s\n", cnt++, ip_string);
		u32 random_tail = rand() % ((1 << (32 - ip_mask) - 0)) + 0;
		u32 ip_int = parse_ip_addr(ip_string);
		int iface_id = -1;
		find_node(root, ip_int, 0, &iface_id);
		fprintf(o_file, "%s %x %d\n", ip_string, ip_int, iface_id);
	}
	//char const_ip_addr1[] = "223.224.154.22";
	//printf("answer1: %d\n", node == NULL ? -1 : node->interface_id);
}

int main() {
	pt_node *root = (pt_node*)malloc(sizeof(pt_node));
	init_node(root);

	build_tree(root);

	output_finding_result(root);
	
	destroy_tree(root);
	system("pause");
	return 0;
}
