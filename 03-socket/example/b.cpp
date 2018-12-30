#include <stdlib.h>
#include <stdio.h>

#define MAX_CNT 700000
#define BUF_SIZE 512
typedef unsigned int u32;
typedef unsigned char u8;

int ip_arr[MAX_CNT];
int mask_arr[MAX_CNT];
int port_arr[MAX_CNT];
int tot;

int parse_ip_addr(char* str) {
	int vec1, vec2, vec3, vec4;
	sscanf(str, "%d.%d.%d.%d", &vec1, &vec2, &vec3, &vec4);
	//printf("%d.%d.%d.%d", vec1, vec2, vec3, vec4);
	return (vec1 << 24) + (vec2 << 16) + (vec3 << 8) + vec4;
}

void read_data() {
	FILE* file = fopen("E:\\UCAS\\wlsyÍøÂçÊµÑé\\09-lookup\\forwarding-table.txt", "r");
	char ip_string[BUF_SIZE];
	int ip_mask;
	int iface_id;
	tot = 0;
	while (!feof(file)) {
		//parse input line
		if (fscanf(file, "%s %d %d", ip_string, &ip_mask, &iface_id) < 0) break;
		//printf("%d %s\n", cnt++, ip_string); //DEBUG
		
		u32 ip_int = parse_ip_addr(ip_string);
		ip_arr[tot] = ip_int;
		mask_arr[tot] = ip_mask;
		port_arr[tot] = iface_id;
		tot++;
		//if (ip_mask<min_mask) min_mask = ip_mask;
		//printf("**%d**%d**%d**\n", ip_int, ip_mask, iface_id);
		//insert_node(root, ip_int, ip_mask, iface_id, 0);
	}
	//printf("%d\n", min_mask);
}

int abs(int x) {
	return x<0?-x:x;
}

void scan_data() {
	int flag = 0;
	int max_mask;
	for (int i = 0; i < tot; i++) {
		if (flag) break;
		if (mask_arr[i] % 2 == 0) {
			for (int j = i+1; j < tot; j++) {
				if (flag) break;
				if (abs(mask_arr[i] - mask_arr[j]) == 1) {
					max_mask = mask_arr[i]>mask_arr[j]?mask_arr[i]:mask_arr[j];
					if ((ip_arr[i] >> (32 - max_mask)) == (ip_arr[j] >> (32 - max_mask))) 
						if (port_arr[i] != port_arr[j]){
							printf("FOUND: %d %d %d %d %d %d\n", i, j, ip_arr[i], mask_arr[i], ip_arr[j], mask_arr[j]);
							flag = 1;
						}
				}	
			}	
		}
	}
}

int main() {
	read_data();
	scan_data();
}
