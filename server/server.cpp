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
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>

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

enum {
    EPOLL_EVENT_OUT,
    EPOLL_EVENT_IN,
    EPOLL_EVENT_ERROR,
    EPOLL_EVENT_INVALID_INPUT,
};


class Epoll {
    enum {
        MAX_EVENT_NUM = 512,
        MAX_CONNECT_NUM = 1024,
    };
    typedef std::set<int> FdSet;
    FdSet _fd_set;
    int _epoll_fd;

    int _epoll_event_num;
    struct epoll_event _epoll_event_out[MAX_EVENT_NUM];
public:
    Epoll(int max_connect_num = MAX_CONNECT_NUM) {
        _epoll_fd = epoll_create(max_connect_num<MAX_CONNECT_NUM?max_connect_num:MAX_CONNECT_NUM);
    };
    ~Epoll() {
        close(_epoll_fd);
    }

    void AddEvent(int fd) {
        struct epoll_event tmp;
        tmp.events = EPOLLIN|EPOLLET;
        tmp.data.fd = fd;
        int ret;
        if (_fd_set.find(fd) != _fd_set.end()) {
            ret = epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &tmp);
        } else {
            ret = epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &tmp);
            if (ret >= 0) {
                _fd_set.insert(fd);
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
        if (_fd_set.find(fd) == _fd_set.end()){
            MLOG(ERROR, "Epoll Del Event Error, Fd: %d", fd);
            return;
        }
        struct epoll_event tmp;
        tmp.data.fd = fd;
        epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, &tmp);
        return;
    }
    inline int Wait(int timeout = 1000) {
        _epoll_event_num = epoll_wait(_epoll_fd, _epoll_event_out, MAX_EVENT_NUM, timeout);
        return _epoll_event_num;
    }
    inline int GetEvent(int i, int &fd) {
        if (i >= _epoll_event_num) return EPOLL_EVENT_INVALID_INPUT;
        unsigned int event = _epoll_event_out[i].events;
        fd = _epoll_event_out[i].data.fd;
        if ((event & EPOLLERR) || (event & EPOLLHUP)) return EPOLL_EVENT_ERROR;
        if (event & POLLIN) return EPOLL_EVENT_IN;
        if (event & POLLOUT) return EPOLL_EVENT_OUT;
        return true;
    }
};

class Shared {
    sem_t *_sem;
    char *_buffer;
    int _size;
    int _cap;
    bool _is_valid;
public:
    Shared(int len = 4000) {
        _is_valid = false;
        len = ((len+sizeof(sem_t)/4096)+1)*4096;
        _buffer = (char *)mmap(0, len, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, -1, 0);
        if (_buffer == (char *)-1) {
            MLOG(ERROR, "Shared Memory Error");
            return;
        }
        _size = 0;
        _cap = len - sizeof(sem_t);
        _sem = (sem_t *)(_buffer + _cap);
        int ret = sem_init(_sem, 1, 1);
        if (ret != 0) {
            MLOG(ERROR, "Sem Init Error");
            return;
        }
        _is_valid = true;
    }
    ~Shared() {
        if (IsValid())
            sem_destroy(_sem);
    }
    inline bool IsValid() const {return _is_valid == true;}
    inline bool IsEmpty() const {return _size == 0;}
    inline const int getCap() const {return _cap;}
    inline const int getSize() const {return _size;}
    void Lock() {
        if (IsValid())
            sem_wait(_sem);
    }
    void Unlock() {
        if (IsValid())
            sem_post(_sem);
    }
    bool Read(char *buffer, int max, int *len) {
        if (_size == 0 || !IsValid()) {
            *len = 0;
            MLOG(INFO, "AAA:%d", _size);
            return false;
        }
        Lock();
        max = _size < max ? _size : max;
        memcpy(buffer, _buffer, max);
        _size = 0;
        Unlock();
        return true;
    }
    bool Write(char *buffer, int len) {
        if (_cap == 0 || !IsValid()) {
            MLOG(ERROR, "Share Memory Write Error");
            return false;
        }
        Lock();
        len = len < _cap ? len : _cap;
        memcpy(_buffer, buffer, len);
        _size = len;
        Unlock();
        return true;
    }
    bool Append(char *buffer, int len) {
        if (_cap == 0 || !IsValid()) {
            MLOG(ERROR, "Share Memory Append Error");
            return false;
        }
        Lock();
        len = len < _cap - _size ? len : _cap - _size;
        memcpy(_buffer, buffer, len);
        _size += len;
        MLOG(INFO, "ABB:%d", _size);
        Unlock();
        return true;
    }
};

class Poll {
public:
    enum {
        POLL_SUCCESS = 0,
        POLL_ERROR,
    };

//private:
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
    SocketMap _socket_map;
    int _listen_fd;
    struct sockaddr_in _listen_addr;        
    Epoll *_p_epoll;
    Shared *_p_shared;

    void addSocketMap(int fd, const struct sockaddr_in *addr) {
        struct SocketInfo *info = new SocketInfo(fd, *addr);
        _socket_map.insert(std::make_pair(fd, *info));
    }
    void delSocketMap(int fd) {
        SocketMap::iterator it = _socket_map.find(fd);
        if (it != _socket_map.end())
            _socket_map.erase(it);
    }
    Octets *getReadBuffer(int fd) {
        SocketMap::iterator it = _socket_map.find(fd);
        if (it != _socket_map.end())
            return &(it->second._rOs);
        return NULL;
    }
    Octets *getWriteBuffer(int fd) {
        SocketMap::iterator it = _socket_map.find(fd);
        if (it != _socket_map.end())
            return &(it->second._wOs);
        return NULL;
    }
    void addNewSocketFather(int fd) {
        _p_shared->Append((char *)&fd, sizeof(fd));
    }

public:
    Poll() {
        _listen_fd = 0;
        memset(&_listen_addr,0,sizeof(_listen_addr));
        _p_epoll = (Epoll *)new Epoll();
        _p_shared = (Shared *)new Shared();
    }
    ~Poll() {
        delete _p_epoll;
        delete _p_shared;
        for (SocketMap::iterator it = _socket_map.begin(); it != _socket_map.end(); it++) {
            MLOG(INFO, "Connect Socket Close, %d", it->first);
            close(it->first);
        }
        MLOG(INFO, "Server Socket Close, %d", _listen_fd);
        if (IsListenFdValid())
            close(_listen_fd);
    }
    inline int InitListen(int port_num) {
        if (_p_epoll == NULL)
            return POLL_ERROR;

        if ((_listen_fd = socket(AF_INET,SOCK_STREAM,0)) < 0) {
            _listen_fd = 0;
            MLOG(ERROR, "Server Socket Error, %d", _listen_fd);
            return POLL_ERROR;
        }
        MLOG(INFO, "Server Socket Open, %d", _listen_fd);

        _listen_addr.sin_family = AF_INET;
        _listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        _listen_addr.sin_port = htons(port_num);
        
        int opt = 1;
        if (setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            MLOG(ERROR, "Set Socket Error, %d", _listen_fd);
            return POLL_ERROR;
        }

        _p_epoll->AddEvent(_listen_fd);
        return POLL_SUCCESS;
    }
    inline bool IsListenFdValid() {return _listen_fd!=0;}
    inline bool IsEpollValid() {return _p_epoll!=NULL;}
    inline int BindAndListen() {
        int ret;
        if ((ret = bind(_listen_fd, (struct sockaddr *)&_listen_addr, sizeof(_listen_addr))) < 0) {
            MLOG(ERROR, "Server Bind Error, %d, %x", ntohs(_listen_addr.sin_port),
                ntohl(_listen_addr.sin_addr.s_addr));
            return ret;
        }
        MLOG(INFO, "Server Bind, %d, %x", ntohs(_listen_addr.sin_port),
            ntohl(_listen_addr.sin_addr.s_addr));
        
        if ((ret = listen(_listen_fd, MAXCONN)) < 0) {
            MLOG(ERROR, "Server Listen Error");
            return ret;
        }

        MLOG(INFO, "Server Listening");
        return 0;
    }
    void SyncNewSocketChild() {
        char buf[100];
        int len;
        _p_shared->Read(buf, 100, &len);
        if (len > 0) {
            int *fds = (int *)&buf[0];
            len /= sizeof(int);
            for (int i = 0; i < len; i++) {
                MLOG(INFO, "Child Sync Fd: %d", fds[i]);
                _p_epoll->AddEvent(fds[i]);
            }
        }
    }
    inline int Execute() {
        int event_num = _p_epoll->Wait();

        if (event_num < 0) {
            MLOG(ERROR, "Poll Wait Error");
            return POLL_ERROR;
        } else if (event_num == 0) {
            return POLL_SUCCESS;
        }

        for (int i = 0; i < event_num; i++) {
            int fd;
            int ret = _p_epoll->GetEvent(i, fd);

            if (IsListenFdValid() && fd == _listen_fd) {
                int connect_fd;
                struct sockaddr connect_addr;        
                unsigned int len;
                connect_fd = accept(_listen_fd, (struct sockaddr *)&connect_addr, (socklen_t *)&len);
                if (connect_fd < 0) {
                    MLOG(ERROR, "Accept Error");
                } else if (len != sizeof(sockaddr_in)) {
                    MLOG(ERROR, "Accept Struct Size Unknown");
                    close(connect_fd);
                    delSocketMap(connect_fd);
                } else {
                    MLOG(INFO, "Accept New Connection: %d", connect_fd);
                    addNewSocketFather(connect_fd);
                    _p_epoll->AddEvent(connect_fd);
                    addSocketMap(connect_fd, (struct sockaddr_in *)&connect_addr);
                }
                continue;
            }

            Octets *os;
            switch(ret) {
            case EPOLL_EVENT_INVALID_INPUT:
                MLOG(ERROR, "Get Event Invalid Input");
                return POLL_ERROR;
            case EPOLL_EVENT_ERROR:
                MLOG(INFO, "Get Event Socket Poll Error, %d", fd);
                _p_epoll->DelEvent(fd);
                close(fd);
                delSocketMap(fd);
                break;
            case EPOLL_EVENT_IN:
                os = getReadBuffer(fd);
                int result;
                result = recv(fd, os->GetCurrent(), os->GetBlank(), 0);
                if (ret <= 0) {
                    MLOG(INFO, "Client Recv Error Fd Close, %d", fd);
                    _p_epoll->DelEvent(fd);
                    close(fd);
                    delSocketMap(fd);
                } else {
                    MLOG(INFO, "Client Receive, Fd: %d, Len: %d", fd, result);
                }
                break;
            case EPOLL_EVENT_OUT:
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
    Poll *poll = new Poll();
    int ret;

    pid_t pid = fork();
    if (pid < 0) {
        MLOG(ERROR, "Fork Error: %d", ret);
        exit(0);
    }

    if (pid > 0) {
        MLOG(INFO, "Father1");
        ret = poll->InitListen(10004);
        if (ret < 0) {
            MLOG(ERROR, "Listen Init Error: %d", ret);
            exit(0);
        }

        ret = poll->BindAndListen();
        if (ret < 0) {
            MLOG(ERROR, "Bind And Listen: %d", ret);
            exit(0);
        } 
    }

    bool new_flag;
    int new_connection;
    while (1) {
        if (pid == 0) {
            poll->SyncNewSocketChild();
            sleep(2);
            continue ;
        }

        ret = poll->Execute();
        if (ret) {
            MLOG(ERROR, "Poll Error: %d", ret);
            break;
        }
    }

    return 0;
}
