/* word counter client: send task message to each server */
/* Client IP Address: 10.0.0.1 */
#include "wordcounterDefinition.h"

#include <unistd.h>
#include <fcntl.h>

int get_file_size(char* filename)
{
    struct stat statbuf;
    stat(filename,&statbuf);
    int size=(int) statbuf.st_size;
 
    return size;
}

int main(int argc, char *argv[])
{
    int sock[ADDR_MAX];
    struct sockaddr_in clientaddr[ADDR_MAX];

    message msg[ADDR_MAX];

    //find the txt file
    if (argc<=1) {
        printf("filename is required.\n");
        exit(0);
    }
    if ((access(argv[1],F_OK))==-1) {
        printf("Could not open file:: no such file or directory\n");
        exit(0);
    }

    //Loading worker address set
    FILE *conf_fp = fopen("workers.conf", "r");
    char server_addr[ADDR_MAX][BUF_SIZE];
    char buf[BUF_SIZE];
    int work_number = 0;
    while (!feof(conf_fp)) {
        fgets(buf, BUF_SIZE, conf_fp);
        buf[strlen(buf) - 1] = '\0'; //turn '\n' into '\0'
        memcpy(server_addr[work_number++], buf, strlen(buf)); 
    }
    work_number--; //no last line: '\n'

    for (int i = 0; i < work_number; i++) {
        sock[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (sock[i] == -1) return -1;
    }
    //printf("Socket created.\n");

    int work_size = get_file_size(argv[1]);
    int startpos = 0;
    int work_step = work_size / work_number;
    printf("work_size: %d work_number: %d step:%d \n", work_size, work_number, work_step);
    
    for (int worker = 0; worker < work_number; worker++) {
        //printf("%s\n", server_addr[worker]);
        clientaddr[worker].sin_addr.s_addr = inet_addr(server_addr[worker]);
        clientaddr[worker].sin_family = AF_INET;
        clientaddr[worker].sin_port = htons(12345);

        if (connect(sock[worker], (struct sockaddr *)&clientaddr[worker], sizeof(struct sockaddr_in)) < 0) {
            printf("worker %d conection failed::check the route\n", worker);
            return -1;
        }
        //printf("to worker%d connected.\n", worker);

        memcpy(msg[worker].path, argv[1], strlen(argv[1]));
        msg[worker].startpos = htonl(startpos);
        msg[worker].endpos = htonl(startpos + work_step);
        if (worker == work_number - 1)
            msg[worker].endpos = htonl(work_size - 1);
        startpos += work_step + 1;

        if (send(sock[worker], &msg[worker], sizeof(message), 0) < 0) return -1;
    }

    int complete_worker_num = 0;
    int complete_worker_arr[ADDR_MAX];
    for (int i = 0; i < work_number; i++) 
        complete_worker_arr[i] = 0;
    while (complete_worker_num < work_number) {
        fd_set rset;
        int max_sock = -1;
        FD_ZERO(&rset);
        for (int i = 0; i < work_number; i++) {
            if (complete_worker_arr[i] == 0) {
                FD_SET(sock[i], &rset);
                if (sock[i] > max_sock) max_sock=sock[i];
            }
        }
        if (select(max_sock+1, &rset, NULL, NULL, NULL) > 0) {
            for (int i = 0; i < work_number; i++) {
                if (FD_ISSET(sock[i], &rset)) {
                    if (recv(sock[i], &msg[i], sizeof(message), 0) < 0) 
                        return -1;
                    else {
                        complete_worker_num++;
                        complete_worker_arr[i] = 1;
                    }
                }
            }
        }
        //printf("%d\n", complete_worker_num);
    }

    int total_cnt[26];
    for (int i = 0; i < 26; i++) {
        total_cnt[i] = 0;
        for (int j = 0; j < work_number; j++)
            total_cnt[i] += ntohl(msg[j].count[i]);
    }
    for (int i = 0; i < 26; i++) {
        printf("%c %d\n", 'a' + i, total_cnt[i]);
    }

    for (int i = 0; i < work_number; i++)
        close(sock[i]);
    fclose(conf_fp);
    return 0;
}