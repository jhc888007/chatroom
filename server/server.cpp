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
#include <set>
#include <map>

#include "log/log.hpp"
#include "proto/net_message.pb.h"

#define MAXLEN 1024
#define MAXCONN 1024
#define MAXEVENT 512
#define MAXTIMEMINUTE 2

class Octets {
    enum {
        MAX_BUFFER_SIZE = 10240,
    };
    int size;
    int capacity;
    char buf[MAX_BUFFER_SIZE];
public:
    Octets() {size = 0; capacity = MAX_BUFFER_SIZE;}
    ~Octets() {;}
    inline int GetSize() {return size;}
    inline int GetCapacity() {return capacity;}
    inline int GetBlank() {return capacity - size;}
    inline char *GetBegin() {return &buf[0];}
    inline char *GetCurrent() {return &buf[size];}
    inline void Clear() {size = 0;};
    int Set(char *x, int len) {
        if (x == NULL || len <= 0 || len >= capacity)
            return -1;
        memcpy(buf, x, len);
        size = len;
    }
    int Add(char *x, int len) {
        if (x == NULL || len <= 0 || len + size > capacity)
            return -1;
        memcpy(buf + size, x, len);
        size += len;
    }
};

class Poll {
public:
    enum {
        EVENT_OUT,
        EVENT_IN,
        EVENT_ERROR,
        EVENT_INVALID_INPUT,
    };
    enum {
        POLL_SUCCESS = 0,
        POLL_ERROR,
    };

    class Epoll {
        enum {
            MAX_EVENT_NUM = 512,
            MAX_CONNECT_NUM = 1024,
        };
        typedef std::set<int> FdSet;
        FdSet fd_set;
        int epoll_fd;

        int epoll_event_num;
        struct epoll_event epoll_event_out[MAX_EVENT_NUM];
    public:
        Epoll(int listen_socket_fd, int max_connect_num = MAX_CONNECT_NUM) {
            epoll_fd = epoll_create(max_connect_num<MAX_CONNECT_NUM?max_connect_num:MAX_CONNECT_NUM);
            AddEvent(listen_socket_fd, EPOLLIN|EPOLLET);
        };
        ~Epoll() {
            close(epoll_fd);
        }

        void AddEvent(int fd, int events) {
            struct epoll_event tmp;
            tmp.events = events;
            tmp.data.fd = fd;
            int ret;
            if (fd_set.find(fd) != fd_set.end()) {
                ret = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &tmp);
            } else {
                ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &tmp);
                if (ret >= 0) {
                    fd_set.insert(fd);
                }
            }
            if (ret) {
                MLOG(ERROR, "Epoll Add Event Error, Fd: %d", fd);
            } else {
                MLOG(INFO, "Epoll Add Event Success, Fd: %d", fd);
            }
            return;
        }
        void DelEvent(int fd) {
            if (fd_set.find(fd) == fd_set.end()){
                MLOG(ERROR, "Epoll Del Event Error, Fd: %d", fd);
                return;
            }
            struct epoll_event tmp;
            tmp.data.fd = fd;
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &tmp);
            return;
        }
        inline int Wait(int timeout = 1000) {
            epoll_event_num = epoll_wait(epoll_fd, epoll_event_out, MAX_EVENT_NUM, timeout);
            return epoll_event_num;
        }
        inline int GetEvent(int i, int &fd) {
            if (i >= epoll_event_num) return Poll::EVENT_INVALID_INPUT;
            unsigned int event = epoll_event_out[i].events;
            fd = epoll_event_out[i].data.fd;
            if ((event & EPOLLERR) || (event & EPOLLHUP)) return Poll::EVENT_ERROR;
            if (event & POLLIN) return Poll::EVENT_IN;
            if (event & POLLOUT) return Poll::EVENT_OUT;
            return true;
        }
    };
         
private:
    struct SocketInfo {
        int _fd;
        struct sockaddr_in _addr;
        Octets _rOs, _wOs;
        SocketInfo(int fd, const struct sockaddr_in &addr) {
            _fd = fd;
            _addr = addr;
        }
    };
    typedef std::map<int, struct SocketInfo> SocketMap;
    SocketMap socket_map;
    int listen_fd;
    struct sockaddr_in listen_addr;        
    Epoll *p_epoll;

    void addSocketMap(int fd, const struct sockaddr_in &addr) {
        struct SocketInfo *info = new SocketInfo(fd, addr);
        socket_map.insert(std::make_pair(fd, *info));
    }
    void delSocketMap(int fd) {
        SocketMap::iterator it = socket_map.find(fd);
        if (it != socket_map.end())
            socket_map.erase(it);
    }
    Octets *getReadBuffer(int fd) {
        SocketMap::iterator it = socket_map.find(fd);
        if (it != socket_map.end())
            return &(it->second._rOs);
        return NULL;
    }
    Octets *getWriteBuffer(int fd) {
        SocketMap::iterator it = socket_map.find(fd);
        if (it != socket_map.end())
            return &(it->second._wOs);
        return NULL;
    }
 
public:
    Poll(int port_num) {
        if ((listen_fd = socket(AF_INET,SOCK_STREAM,0)) < 0) {
            listen_fd = 0;
            MLOG(ERROR, "Server Socket Error, %d", listen_fd);
            return ;
        }
        MLOG(INFO, "Server Socket Open, %d", listen_fd);

        memset(&listen_addr,0,sizeof(listen_addr));
        listen_addr.sin_family = AF_INET;
        listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        listen_addr.sin_port = htons(port_num);
        p_epoll = (Epoll *)new Epoll(listen_fd);
    }
    ~Poll() {
        delete p_epoll;
        for (SocketMap::iterator it = socket_map.begin(); it != socket_map.end(); it++) {
            MLOG(INFO, "Connect Socket Close, %d", it->first);
            close(it->first);
        }
        MLOG(INFO, "Server Socket Close, %d", listen_fd);
        close(listen_fd);
    }
    inline bool IsValid() {return listen_fd==0;}
    inline int BindAndListen() {
        int ret;
        if ((ret = bind(listen_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr))) < 0) {
            MLOG(ERROR, "Server Bind Error, %d, %x", ntohs(listen_addr.sin_port),
                ntohl(listen_addr.sin_addr.s_addr));
            return ret;
        }
        MLOG(INFO, "Server Bind, %d, %x", ntohs(listen_addr.sin_port),
            ntohl(listen_addr.sin_addr.s_addr));
        
        if ((ret = listen(listen_fd, MAXCONN)) < 0) {
            MLOG(ERROR, "Server Listen Error");
            return ret;
        }

        MLOG(INFO, "Server Listening");
        return 0;
    }
    inline int Execute() {
        int event_num = p_epoll->Wait();

        if (event_num < 0) {
            MLOG(ERROR, "Poll Wait Error");
            return POLL_ERROR;
        } else if (event_num == 0) {
            return POLL_SUCCESS;
        }

        for (int i = 0; i < event_num; i++) {
            int fd;
            int ret = p_epoll->GetEvent(i, fd);

            if (fd == listen_fd) {
                int connect_fd;
                struct sockaddr_in connect_addr;        
                unsigned int len;
                connect_fd = accept(listen_fd, (struct sockaddr *)&listen_addr, (socklen_t *)&len);
                if (connect_fd < 0) {
                    MLOG(ERROR, "Accept Error");
                } else if (len != sizeof(connect_addr)) {
                    MLOG(ERROR, "Accept Struct Size Unknown");
                    close(connect_fd);
                } else {
                    p_epoll->AddEvent(connect_fd, EPOLLIN|EPOLLET);
                    addSocketMap(connect_fd, connect_addr);
                }
                continue;
            }

            Octets *os;
            switch(ret) {
            case EVENT_INVALID_INPUT:
                MLOG(ERROR, "Get Event Invalid Input");
                return POLL_ERROR;
            case EVENT_ERROR:
                MLOG(INFO, "Get Event Socket Poll Error");
                p_epoll->DelEvent(fd);
                close(fd);
                break;
            case EVENT_IN:
                os = getReadBuffer(fd);
                int result;
                result = recv(fd, os->GetCurrent(), os->GetBlank(), 0);
                if (ret <= 0) {
                    MLOG(INFO, "Client Recv Error Fd Close, %d", fd);
                    p_epoll->DelEvent(fd);
                    close(fd);
                } else {
                    MLOG(INFO, "Client Receive, Fd: %d, Len: %d", fd, result);
                }
                break;
            case EVENT_OUT:
                os = getWriteBuffer(fd);
                MLOG(INFO, "Client Send, Fd: %d", fd);
                break;
            default:
                MLOG(ERROR, "Get Event Unknown: %d", ret);
                return POLL_ERROR;
            }
        }
        return POLL_SUCCESS;
    }
};

int main(int argc,char **argv) {
    Poll *poll = new Poll(10004);
    int ret = poll->BindAndListen();
    if (ret < 0) {
        MLOG(ERROR, "Bind And Listen: %d", ret);
        exit(0);
    } 
    while (1) {
        ret = poll->Execute();
        if (ret) {
            MLOG(ERROR, "Poll Error: %d", ret);
            break;
        }
    }
    return 0;
}
