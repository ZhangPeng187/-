#ifndef __EPOLL_H__
#define __EPOLL_H__
#include "requestData.h"
#include "timer.h"
#include <vector>
#include <unordered_map>
#include <sys/epoll.h>
#include <memory>

class Epoll
{
public:
    typedef std::shared_ptr<RequestData> SP_ReqData;
private:
    static epoll_event *events;
    static std::unordered_map<int, SP_ReqData> fd2req;
    static int epoll_fd;
    static const std::string PATH;

    static TimerManager timer_manager;
    // std::shared_ptr<requestData> r = std::make_shared<requestData>(nullptr);
public:
    static int epoll_init(int max_events, int listen_num);
    static int epoll_add(int fd, SP_ReqData request, __uint32_t events);
    static int epoll_mod(int fd, SP_ReqData request, __uint32_t events);
    static int epoll_del(int fd, __uint32_t events = (EPOLLIN | EPOLLET | EPOLLONESHOT));
    static void my_epoll_wait(int listen_fd, int max_events, int timeout);
    static void acceptConnection(int listen_fd, int epoll_fd, const std::string path);
    static void getEventsRequset(std::vector<SP_ReqData> &req_data, int listen_fd, int events_num, std::string path);
    static void add_timer(SP_ReqData request_data, int timeout);
};

/**
 *  struct epoll_event
    {
        uint32_t events;	    Epoll events
        epoll_data_t data;    User data variable
    } __EPOLL_PACKED;

    typedef union epoll_data
    {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
    } epoll_data_t;
    * 
*/
#endif