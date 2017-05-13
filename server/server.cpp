#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <vector>

#include "log/log.hpp"

#define MAXLINE 1024
#define MAXCONN 1024

void *server_thread(void *arg) {
    int connect_socket_id = *(int*)arg;
    char buff[MAXLINE];
    int n;
    MLOG(INFO, "Server Connected");
    for (;;) {
        n = recv(connect_socket_id, buff, MAXLINE, 0);
        if (n <= 0) break;
        buff[n] = '\0';
        MLOG(INFO, "Recv Msg From Client: %s, Len: %d", buff, n);
    }
    MLOG(INFO, "Server Release");
    close(connect_socket_id);
    return NULL;
}

int main(int argc,char **argv) {
    int listen_socket_id;
    if ((listen_socket_id = socket(AF_INET,SOCK_STREAM,0)) < 0) {
        MLOG(ERROR, "Server Socket Error, %d", listen_socket_id);
        exit(0);
    }
    MLOG(INFO, "Server Socket, %d", listen_socket_id);
    
    struct sockaddr_in sockaddr;
    memset(&sockaddr,0,sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    sockaddr.sin_port = htons(10004);
    if (bind(listen_socket_id, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) < 0) {
        MLOG(ERROR, "Server Bind Error, %d, %x", ntohs(sockaddr.sin_port),
            ntohl(sockaddr.sin_addr.s_addr));
        exit(0);
    }
    MLOG(INFO, "Server Bind, %d, %x", ntohs(sockaddr.sin_port),
        ntohl(sockaddr.sin_addr.s_addr));
    
    if (listen(listen_socket_id, MAXCONN) < 0) {
        MLOG(ERROR, "Server Listen Error");
        exit(0);
    }
    MLOG(INFO, "Server Listening");

    int connect_socket_id;
    std::vector<pthread_t> thread_vector;
    for (;;) {
        if ((connect_socket_id = accept(listen_socket_id, (struct sockaddr*)NULL, NULL))==-1) {
            MLOG(ERROR, "Server Accpet Error: %s Errno: %d", strerror(errno), errno);
            continue;
        }
        
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, &server_thread, &connect_socket_id) != 0) {
            MLOG(ERROR, "Thread Create Fail, Socket: %d", connect_socket_id);
            close(connect_socket_id);
        }
        thread_vector.push_back(thread_id);
        MLOG(INFO, "Thread Create Success, Thread: %d", (int)thread_id);

        /*pid_t pid = fork();
        if (pid == -1) {
            MLOG(ERROR, "Server Fork Fail");
        }
        if (pid == 0) {
            MLOG(INFO, "Server Connected");
            close(listen_socket_id);
            for (;;) {
                n = recv(connect_socket_id, buff, MAXLINE, 0);
                if (n <= 0) break;
                buff[n] = '\0';
                MLOG(INFO, "Recv Msg From Client: %s, Len: %d", buff, n);
            }
            MLOG(INFO, "Server Release");
            close(connect_socket_id);
            exit(0);
        } else {
            MLOG(INFO, "Server New Fork Pid: %d", (int)pid);
            close(connect_socket_id);
        }*/
        //shutdown(connect_socket_id, SHUT_RDWR);
    }

    for (std::vector<pthread_t>::iterator it = thread_vector.begin(); it != thread_vector.end(); it++) {
        pthread_join(*it, NULL);
    }

    close(listen_socket_id);
}
