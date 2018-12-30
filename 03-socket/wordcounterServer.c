/* server: take the task message and return counter's results */
#include "wordcounterDefinition.h"

int main(int argc, const char *argv[]) {
	int serversock, clientsock;
	struct sockaddr_in serveraddr, clientaddr;
	message msg;

	//create socket
	if ((serversock = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
		return -1;
	//printf("worker created.\n");

	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = INADDR_ANY;
	serveraddr.sin_port = htons(12345);

	//bind
	if (bind(serversock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
		return -1;
	//printf("worker binded.\n");

	listen(serversock, 1);
	//printf("waiting my task...");

	int clientsockaddr = sizeof(struct sockaddr_in);
	if ((clientsock = accept(serversock, (struct sockaddr *)&clientaddr, (socklen_t *)&clientsockaddr)) < 0) 
		return -1;
	//printf("connection accepted.\n");

	if (recv(clientsock, &msg, sizeof(msg), 0) > 0) {
		FILE * work_fp = fopen(msg.path, "r");
		msg.startpos = ntohl(msg.startpos);
		msg.endpos = ntohl(msg.endpos);

		int pos = msg.startpos;
		char c;
		fseek(work_fp, pos, SEEK_SET);

		for (int i=0; i < 26; i++) {
			msg.count[i] = 0;
		}
		printf("my job: %d from %d\n", msg.startpos, msg.endpos);
		while (pos<msg.endpos) {
			c = fgetc(work_fp);
			if (c>='A' && c<='Z') c = c - 'A' + 'a';
			if (c>='a' && c<='z') msg.count[c - 'a']++;
			pos++;
		}
		for (int i = 0; i < 26; i++) {
			msg.count[i] = htonl(msg.count[i]);
		}
		write(clientsock, &msg, sizeof(msg));
		//DEBUGGING
		/*char buf[BUF_SIZE];
		fgets(buf, 20, work_fp);
		printf("%s\n", buf);*/
		// for (int i = 0; i < 26; i++) {
		// 	printf("%c: %d\n", 'A' + i, ntohl(msg.count[i]));
		// }

		fclose(work_fp);
	}
	else {
		printf("connection failed.\n");
	}

	close(serversock);
	return 0;
}
