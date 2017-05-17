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
#include <time.h>
#include <poll.h>
#include <sys/epoll.h>

#include "log/log.hpp"
#include "proto/net_message.pb.h"

#define MAXLINE 1024
#define MAXCONN 1024
#define MAXEVENT 512
#define MAXTIMEMINUTE 2

void temp(pb::Login lo) {
    return;
}

void *server_thread(void *arg) {
    int connect_fd = *(int*)arg;
    char buff[MAXLINE];
    int n;
    MLOG(INFO, "Server Connected");
    for (;;) {
        n = recv(connect_fd, buff, MAXLINE, 0);
        if (n <= 0) break;
        buff[n] = '\0';
        MLOG(INFO, "Recv Msg From Client: %s, Len: %d", buff, n);
    }
    MLOG(INFO, "Server Release");
    close(connect_fd);
    return NULL;
}

int main(int argc,char **argv) {
    int listen_fd;
    if ((listen_fd = socket(AF_INET,SOCK_STREAM,0)) < 0) {
        MLOG(ERROR, "Server Socket Error, %d", listen_fd);
        exit(0);
    }
    MLOG(INFO, "Server Socket, %d", listen_fd);
    
    struct sockaddr_in sockaddr;
    memset(&sockaddr,0,sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    sockaddr.sin_port = htons(10004);
    if (bind(listen_fd, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) < 0) {
        MLOG(ERROR, "Server Bind Error, %d, %x", ntohs(sockaddr.sin_port),
            ntohl(sockaddr.sin_addr.s_addr));
        exit(0);
    }
    MLOG(INFO, "Server Bind, %d, %x", ntohs(sockaddr.sin_port),
        ntohl(sockaddr.sin_addr.s_addr));
    
    if (listen(listen_fd, MAXCONN) < 0) {
        MLOG(ERROR, "Server Listen Error");
        exit(0);
    }
    MLOG(INFO, "Server Listening");

    int connect_fd, max_socket_id;
    
    //Thread
    std::vector<pthread_t> thread_vector;

    //Select
    fd_set fdsr;
    int fd[MAXCONN];

    //Poll
    struct pollfd cfd[MAXCONN];
    memset((void*)cfd, 0, sizeof(struct pollfd)*MAXCONN);
    cfd[0].fd = listen_fd;
    cfd[0].events = POLLIN;
    max_socket_id = listen_fd;

    //EPoll
    int epoll_fd;
    struct epoll_event es[MAXEVENT], tmp_event;
    epoll_fd = epoll_create(MAXCONN);
    {
        tmp_event.events = EPOLLIN|EPOLLET;
        tmp_event.data.fd = listen_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &tmp_event) < 0) {
            MLOG(ERROR, "EPoll Init Error");
            exit(1);
        }
    }

    time_t time_fd[MAXCONN];
    time_t time_now;
    for (;;) {
        //EPoll
        {
            int timeout = 2000;
            int ret = epoll_wait(epoll_fd, es, MAXEVENT, timeout);

            if (ret < 0) {
                MLOG(ERROR, "EPoll Error");
                break;
            } else if (ret == 0) {
                continue;
            }

            int efd;
            unsigned int eflag;
            for (int i = 0; i < ret; i++) {
                efd = es[i].data.fd;
                eflag = es[i].events;
                if ((eflag & EPOLLERR) ||
                    (eflag & EPOLLHUP) ||
                    !(eflag & EPOLLIN))
                {
                    MLOG(ERROR, "EPoll Event Error: %d", eflag);
                    close(efd);
                    exit(1);
                }

                if (efd == listen_fd) {
                    connect_fd = accept(listen_fd, NULL, NULL);

                    if (connect_fd < 0) {
                        MLOG(ERROR, "Accept Error");
                    } else {
                        tmp_event.data.fd = connect_fd;
                        tmp_event.events = EPOLLIN|EPOLLET;
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connect_fd, &tmp_event);
                    }

                    continue;
                }

                int len;
                char buff[MAXLINE];
                len = recv(efd, buff, MAXLINE, 0);
                if (len <= 0) {
                    MLOG(INFO, "Client Close, %d", efd);
                    close(efd);
                } else {
                    buff[len] = 0;
                    MLOG(INFO, "Client Receive, Fd: %d, Len: %d, Msg: %.*s", efd, len, len, buff);
                }
            }
        }

        //Poll
        /*
        {
            time_now = time((time_t*)NULL);

            int timeout = 1000;
            int ret = poll(cfd, max_socket_id+1, timeout);

            if (ret < 0) {
                MLOG(ERROR, "Poll Error");
                break;
            } else if (ret == 0) {
                continue;
            }

            if (cfd[0].revents & POLLIN) {
                connect_fd = accept(listen_fd, NULL, NULL);

                if (connect_fd < 0) {
                    MLOG(ERROR, "Accept Error");
                } else {
                    int loop;
                    for (loop = 0; loop < MAXCONN; loop++) {
                        if (cfd[loop].fd <= 0) {
                            cfd[loop].fd = connect_fd;
                            cfd[loop].events = POLLIN;
                            time_fd[loop] = time_now;
                            if (connect_fd > max_socket_id) {
                                max_socket_id = connect_fd;
                            }
                            break;
                        }
                    }
                    if (loop == MAXCONN) {
                        MLOG(ERROR, "Too Many Connection");
                        exit(1);
                    }
                }
            }

            int len;
            char buff[MAXLINE];
            for (int i = 1; i <= MAXCONN; i++) {
                if (cfd[i].fd <= 0 ) {
                    continue;
                }
                if (time_now - time_fd[i] > 60) {
                    MLOG(INFO, "Client Close, %d", fd[i]);
                    close(cfd[i].fd);
                    cfd[i].fd = 0;
                }
                if (cfd[i].revents & (POLLIN|POLLERR)) {
                    len = recv(cfd[i].fd, buff, MAXLINE, 0);
                    if (len <= 0) {
                        MLOG(INFO, "Client Close, %d", fd[i]);
                        close(cfd[i].fd);
                        cfd[i].fd = 0;
                    } else {
                        buff[len] = 0;
                        time_fd[i] = time_now;
                        MLOG(INFO, "Client Receive, Fd: %d, Len: %d, Msg: %.*s", cfd[i].fd, len, len, buff);
                    }
                }
            }
        }
        */
        
        //Select
        /*
        {
            time_now = time((time_t*)NULL);

            FD_ZERO(&fdsr);
            FD_SET(listen_fd, &fdsr);
            MLOG(INFO, "Server Fd: %d", listen_fd);
            max_socket_id = listen_fd;
            for (int i = 0; i < MAXCONN; i++) {
                if (fd[i] == 0) {
                    continue;
                }
                FD_SET(fd[i], &fdsr);
                MLOG(INFO, "Client Fd: %d", fd[i]);
                if (time_now - time_fd[i] > 60) {
                    MLOG(INFO, "Client Timeout, %d", fd[i]);
                    close(fd[i]);
                    FD_CLR(fd[i], &fdsr);
                    fd[i] = 0;
                }
                if (fd[i] && fd[i] > max_socket_id) {
                    max_socket_id = fd[i];
                }
            }

            struct timeval timeout;
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;
            int ret = select(max_socket_id+1, &fdsr, NULL, NULL, &timeout);

            if (ret < 0) {
                MLOG(ERROR, "Select Error");
                break;
            } else if (ret == 0) {
                continue;
            }

            MLOG(INFO, "Select Something");
            int len;
            char buff[MAXLINE];
            for (int i = 0; i < MAXCONN; i++) {
                if (FD_ISSET(fd[i], &fdsr)) {
                    len = recv(fd[i], buff, MAXLINE, 0);
                    if (len <= 0) {
                        MLOG(INFO, "Client Close, %d", fd[i]);
                        close(fd[i]);
                        FD_CLR(fd[i], &fdsr);
                        fd[i] = 0;
                    } else {
                        buff[len] = 0;
                        time_fd[i] = time_now;
                        MLOG(INFO, "Client Receive, Fd: %d, Len: %d, Msg: %.*s", fd[i], len, len, buff);
                    }
                }
            }

            if (FD_ISSET(listen_fd, &fdsr)) {
                if ((connect_fd = accept(listen_fd, (struct sockaddr*)NULL, NULL))==-1) {
                    MLOG(ERROR, "Server Accpet Error: %s Errno: %d", strerror(errno), errno);
                    continue;
                }
                int i;
                for (i = 0; i < MAXCONN; i++) {
                    if (fd[i] == 0) {
                        fd[i] = connect_fd;
                        time_fd[i] = time_now;
                        MLOG(INFO, "Server Accpet: %d", connect_fd);
                        break;
                    }
                }
                if (i >= MAXCONN) {
                    MLOG(INFO, "Server Full, Close: %d", connect_fd);
                    close(connect_fd);
                }
            }
        }
        */

        //Multithread
        /*
        if (0) {
            if ((connect_fd = accept(listen_fd, (struct sockaddr*)NULL, NULL))==-1) {
                MLOG(ERROR, "Server Accpet Error: %s Errno: %d", strerror(errno), errno);
                continue;
            }
            
            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, &server_thread, &connect_fd) != 0) {
                MLOG(ERROR, "Thread Create Fail, Socket: %d", connect_fd);
                close(connect_fd);
            }
            thread_vector.push_back(thread_id);
            MLOG(INFO, "Thread Create Success, Thread: %d", (int)thread_id);
        }
        */

        //Single
        /*
        if (0) {
            if ((connect_fd = accept(listen_fd, (struct sockaddr*)NULL, NULL))==-1) {
                MLOG(ERROR, "Server Accpet Error: %s Errno: %d", strerror(errno), errno);
                continue;
            }
            
            pid_t pid = fork();
            if (pid == -1) {
                MLOG(ERROR, "Server Fork Fail");
            }
            if (pid == 0) {
                MLOG(INFO, "Server Connected");
                close(listen_fd);
                for (;;) {
                    n = recv(connect_fd, buff, MAXLINE, 0);
                    if (n <= 0) break;
                    buff[n] = '\0';
                    MLOG(INFO, "Recv Msg From Client: %s, Len: %d", buff, n);
                }
                MLOG(INFO, "Server Release");
                close(connect_fd);
                exit(0);
            } else {
                MLOG(INFO, "Server New Fork Pid: %d", (int)pid);
                close(connect_fd);
            }
        }
        */
    }
    
    //EPoll
    close(epoll_fd);

    //Multithread
    /*
    {
        for (std::vector<pthread_t>::iterator it = thread_vector.begin(); it != thread_vector.end(); it++) {
            pthread_join(*it, NULL);
    }
    */

    close(listen_fd);
}
