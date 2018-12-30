#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h> 
#include <sys/select.h>  
#include <unistd.h>
#include <stdlib.h>

#define BUF_SIZE 100
#define ADDR_MAX 100

typedef struct message {
	char path[ADDR_MAX]; //master -> worker
	int startpos; //master -> worker
	int endpos; //master -> worker
	int count[26]; //worker -> master
} message;
