#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "log/log.hpp"

#define MAXLINE 1024
#define MAXCONN 1024


class A {
public:
    int a;
    A() { printf("A()\n"); }
    ~A() { printf("~A()\n"); }
    A(const A &c) { a = c.a; printf("CopyA()\n"); }
};

int main(int argc,char **argv) {
    int listen_socket_id;
    if ((listen_socket_id = socket(AF_INET,SOCK_STREAM,0)) < 0) {
        MLOG_ERROR("Server Socket Error, %d", listen_socket_id);
        exit(0);
    }
    MLOG_INFO("Server Socket, %d", listen_socket_id);
    
    struct sockaddr_in sockaddr;
    memset(&sockaddr,0,sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    sockaddr.sin_port = htons(10004);
    if (bind(listen_socket_id, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) < 0) {
        MLOG_ERROR("Server Bind Error, %d, %x", ntohs(sockaddr.sin_port),
            ntohl(sockaddr.sin_addr.s_addr));
        exit(0);
    }
    MLOG_INFO("Server Bind, %d, %x", ntohs(sockaddr.sin_port),
        ntohl(sockaddr.sin_addr.s_addr));
    
    if (listen(listen_socket_id, MAXCONN) < 0) {
        MLOG_ERROR("Server Listen Error");
        exit(0);
    }
    MLOG_INFO("Server Listening");

    int connect_socket_id;
    char buff[MAXLINE];
    int n;
    A aaa;
    aaa.a = 0;
    for (;;) {
        if ((connect_socket_id = accept(listen_socket_id, (struct sockaddr*)NULL, NULL))==-1) {
            MLOG_ERROR("Server Accpet Error: %s Errno: %d", strerror(errno), errno);
            continue;
        }
        printf("aaaaa%d\n", aaa.a);
        
        pid_t pid = fork();
        if (pid == -1) {
            MLOG_ERROR("Server Fork Fail");
        }
        if (pid == 0) {
            printf("aaa%d\n", ++aaa.a);
            MLOG_INFO("Server Connected");
            close(listen_socket_id);
            for (;;) {
                n = recv(connect_socket_id, buff, MAXLINE, 0);
                if (n <= 0) break;
                buff[n] = '\0';
                MLOG_INFO("Recv Msg From Client: %s, Len: %d", buff, n);
            }
            MLOG_INFO("Server Release");
            close(connect_socket_id);
            exit(0);
        } else {
            MLOG_INFO("Server New Fork Pid: %d", (int)pid);
            close(connect_socket_id);
        }
        //shutdown(connect_socket_id, SHUT_RDWR);
    }
    close(listen_socket_id);
}
