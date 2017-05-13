#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "log/log.hpp"

#define MAXLINE 1024

int main(int argc,char **argv)
{
    int socket_id;
    if ((socket_id = socket(AF_INET,SOCK_STREAM,0)) < 0) {
        MLOG_ERROR("Socket Add Error: %d", socket_id);
        exit(0);
    }
    MLOG_INFO("Socket Add: %d", socket_id);

    struct sockaddr_in sockaddr;
    const char *servInetAddr = "127.0.0.1";
    memset(&sockaddr,0,sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(10004);
    inet_pton(AF_INET,servInetAddr,&sockaddr.sin_addr);
    
    if ((connect(socket_id,(struct sockaddr*)&sockaddr,sizeof(sockaddr))) < 0) {
        MLOG_ERROR("Socket Connect Error %s Errno: %d", strerror(errno),errno);
        exit(0);
    }
    MLOG_INFO("Socket Connected");

    for (int i = 0; i < 5; i++) {
        char sendline[MAXLINE] = "Send Msg";
        int n;
        if ((send(socket_id,sendline,strlen(sendline),0)) < 0) {
            MLOG_ERROR("Send Msg Error %s Errno: %d", strerror(errno),errno);
            exit(0);
        }
        MLOG_INFO("Socket Send Msg");
        sleep(3);
    }

    close(socket_id);
    exit(0);
}


