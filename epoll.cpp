#include "epoll.h"
#include "util.h"
#include "threadpool.h"
#include "requestData.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <queue>
#include <deque>
#include <string.h>
#include <mutex>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
using namespace std;

int TIMER_TIME_OUT = 500;
epoll_event *Epoll::events;
unordered_map<int, shared_ptr<RequestData>> Epoll::fd2req;
int Epoll::epoll_fd = 0;
const string Epoll::PATH = "/";

TimerManager Epoll::timer_manager;

int Epoll::epoll_init(int max_events, int listen_num)
{
    epoll_fd = epoll_create(listen_num + 1);
    if (epoll_fd == -1)
        return -1;

    events = new epoll_event[max_events];
    return 0;
}

// 注册新描述符
int Epoll::epoll_add(int fd, SP_ReqData request, __uint32_t events)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    fd2req[fd] = request;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        perror("epoll_add error");
        fd2req.erase(fd);
        return -1;
    }
    return 0;
}

// 修改状态描述符
int Epoll::epoll_mod(int fd, SP_ReqData request, __uint32_t events)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;

    fd2req[fd] = request;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) < 0)
    {
        perror("epoll_mod error");
        fd2req.erase(fd);
        return -1;
    }

    return 0;
}

// 从epoll中删除描述符
int Epoll::epoll_del(int fd, __uint32_t events)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    // printf("del to epoll %d\n", fd);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &event) < 0)
    {
        perror("epoll_del error");
        return -1;
    }
    if (fd2req.count(fd))
        fd2req.erase(fd);
    return 0;
}

// 处理活跃事件数
void Epoll::my_epoll_wait(int listen_fd, int max_events, int timeout)
{

    int nfds = epoll_wait(epoll_fd, events, max_events, timeout);
    if (nfds < 0)
        perror("epoll wait error");
    vector<SP_ReqData> req_data;
    getEventsRequset(req_data, listen_fd, nfds, PATH);
    if (req_data.size() > 0)
    {
        for (auto &req : req_data)
        {
            if (ThreadPool::threadpool_add(req) < 0)
            {
                // 线程池满了或者关闭了等原因，抛弃本次监听到的请求
                break;
            }
        }
    }
    timer_manager.handle_expired_event();
}

// 分发处理函数
void Epoll::getEventsRequset(vector<SP_ReqData> &req_data, int listen_fd, int events_num, string path)
{
    for (int i = 0; i < events_num; ++i)
    {
        // 获取有事件产生的标识符
        int fd = events[i].data.fd;

        if (fd == listen_fd)
        {
            acceptConnection(listen_fd, epoll_fd, path);
        }
        else if (fd < 3)
        {
            continue;
        }
        else
        {
            // 排除错误事件
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN)))
            {
                // cout << "error event" << endl;
                if (fd2req.count(fd))
                    fd2req.erase(fd);
                continue;
            }

            // 将请求任务加入线程池中
            // 加入线程池之前将Timer和request分离

            fd2req[fd]->seperateTimer();
            req_data.emplace_back(fd2req[fd]);
            fd2req.erase(fd);
        }
    }
}
void Epoll::acceptConnection(int listen_fd, int epoll_fd, string path)
{
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    socklen_t client_addr_len = sizeof(client_addr);
    // 通信套接字
    int accept_fd = 0;
    /*
        每当捕获到通信套接字，服务器端就进行接收，创建requestData*对象保存通信套接字，注册epoll事件，
        并为requestData*对象创建时钟信息。为了可以通过时钟信息获取到对应的套接字信息，
        故mytimer对象中保存requestData*信息，类似于hsah表。

    */
    while ((accept_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len)) > 0)
    {
        // cout << inet_ntoa(client_addr.sin_addr) << endl;
        // cout << ntohs(client_addr.sin_port) << endl;
        /*
        // TCP 的保活机制默认是关闭的
        int optval = 0;
        socklen_t len_optval = 4;
        getsockopt(accept_fd, SOL_SOCKET, SO_KEEPALIVE, &optval, &len_optval);
        cout << "optval = " << optval << endl;
        */

        // 设置为 非阻塞模式
        int ret = setSocketNonBlocking(accept_fd);
        if (ret < 0)
        {
            perror("Set non block failed!");
            return;
        }

        SP_ReqData req_info(new RequestData(epoll_fd, accept_fd, path));

        // 文件描述符可以读，边缘触发模式（ET），保证一个socket连接在任一时刻只能被一个线程处理
        // EPOLLONESHOT 多线程中使用，保证了一个socket连接在任一时刻都只能被一个线程处理
        __uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
        Epoll::epoll_add(accept_fd, req_info, _epo_event);
        // 新增时间信息
        timer_manager.addTimer(req_info, TIMER_TIME_OUT);
    }
    // 错误码11，表示EAGAIN（Resource temporarily unavailable）
    // 表明在非阻塞模式下调用了阻塞操作，在该操作没有完成就返回这个错误。
    // 这个错误不会破坏socket的同步性，不用关心，下次循环接着接收即可
    // if (accept_fd == -1)
    // {
    //     perror("acceptConnection accept");
    //     fprintf(stderr, "error is %d_%s", errno, strerror(errno));
    // }
}

void Epoll::add_timer(Epoll::SP_ReqData request_data, int timeout)
{
    timer_manager.addTimer(request_data, timeout);
}