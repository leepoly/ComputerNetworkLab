#include "tcp_sock.h"

#include "log.h"

#include <unistd.h>

#define BUF_SIZE 1024

// tcp server application, listens to port (specified by arg) and serves only one
// connection request
void *tcp_server(void *arg)
{
	u16 port = *(u16 *)arg;
	struct tcp_sock *tsk = alloc_tcp_sock();

	struct sock_addr addr;
	addr.ip = htonl(0);
	addr.port = port;
	if (tcp_sock_bind(tsk, &addr) < 0) {
		log(ERROR, "tcp_sock bind to port %hu failed", ntohs(port));
		exit(1);
	}

	if (tcp_sock_listen(tsk, 3) < 0) {
		log(ERROR, "tcp_sock listen failed");
		exit(1);
	}

	log(DEBUG, "listen to port %hu.", ntohs(port));

	struct tcp_sock *csk = tcp_sock_accept(tsk);

	log(DEBUG, "accept a connection.");

	char rbuf[BUF_SIZE];
	FILE *file = fopen("server-output.dat", "wb");
	int chunk_num = 0;
	while (1) {
		int rlen = tcp_sock_read(csk, rbuf, 1000);
		if (rlen < 0) {
			log(DEBUG, "tcp_sock_read return negative value, finish transmission.");
			break;
		} 
		else if (rlen > 0) {
			rbuf[rlen] = '\0';
			fwrite(rbuf, 1, rlen, file);
			printf("server: recv No.%d chunk.\n", chunk_num++);
			/*if (tcp_sock_write(csk, wbuf, strlen(wbuf)) < 0) {
				log(DEBUG, "tcp_sock_write return negative value, finish transmission.");
				break;
			}*/
		}
	}

	//tcp_sock_write(csk, rbuf, 1);
	// char rbuf[1001];
	// char wbuf[1024];
	// int rlen = 0;
	// while (1) {
	// 	rlen = tcp_sock_read(csk, rbuf, 1000);
	// 	if (rlen < 0) {
	// 		log(DEBUG, "tcp_sock_read return negative value, finish transmission.");
	// 		break;
	// 	} 
	// 	else if (rlen > 0) {
	// 		rbuf[rlen] = '\0';
	// 		sprintf(wbuf, "server echoes: %s", rbuf);
	// 		if (tcp_sock_write(csk, wbuf, strlen(wbuf)) < 0) {
	// 			log(DEBUG, "tcp_sock_write return negative value, finish transmission.");
	// 			break;
	// 		}
	// 	}
	// }
	fclose(file);
	log(DEBUG, "close this connection.");
	printf("server: close tsk.\n");
	tcp_sock_close(csk);
	
	return NULL;
}

// tcp client application, connects to server (ip:port specified by arg), each
// time sends one bulk of data and receives one bulk of data 
void *tcp_client(void *arg)
{
	struct sock_addr *skaddr = arg;

	struct tcp_sock *tsk = alloc_tcp_sock();

	if (tcp_sock_connect(tsk, skaddr) < 0) {
		log(ERROR, "tcp_sock connect to server ("IP_FMT":%hu)failed.", \
				NET_IP_FMT_STR(skaddr->ip), ntohs(skaddr->port));
		exit(1);
	}

	char buf[BUF_SIZE];
	int chunk_num = 0;
	FILE *file = fopen("client-input.dat", "rb");
	while (!feof(file)) {
        int ret_size = fread(buf, 1, BUF_SIZE, file);
        tcp_sock_write(tsk, buf, ret_size);
        printf("client: sent No.%d chunk.\n", chunk_num++);
        if (ret_size < BUF_SIZE) break;
		sleep(1);
    }

    //tcp_sock_read(tsk, buf, 1000);

	// char *wbuf = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	// int wlen = strlen(wbuf);
	// char rbuf[1001];
	// int rlen = 0;

	// int n = 10;
	// for (int i = 0; i < n; i++) {
	// 	if (tcp_sock_write(tsk, wbuf + i, wlen - n) < 0)
	// 		break;

	// 	rlen = tcp_sock_read(tsk, rbuf, 1000);
	// 	if (rlen < 0) {
	// 		log(DEBUG, "tcp_sock_read return negative value, finish transmission.");
	// 		break;
	// 	}
	// 	else if (rlen > 0) {
	// 		rbuf[rlen] = '\0';
	// 		fprintf(stdout, "%s\n", rbuf);
	// 	}
	// 	else {
	// 		fprintf(stdout, "*** read data == 0.\n");
	// 	}
	// 	sleep(1);
	// }
    fclose(file);
    printf("client: close tsk.\n");
	tcp_sock_close(tsk);

	return NULL;
}
