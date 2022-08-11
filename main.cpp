#include "requestData.h"
#include "epoll.h"
#include "threadpool.h"
#include "util.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdlib>
#include <vector>
#include <queue>
#include <cstring>
#include <sys/time.h>
#include <iostream>
#include <signal.h>

using namespace std;

static const int MAXEVENTS = 5000;
static const int LISTENQ = 1024;

const int THREADPOOL_THREAD_NUM = 10;
const int QUEUE_SIZE = 1024;

const int PORT = 8887;
const int ASK_STATIC_FILE = 1;
const int ASK_IMAGE_STITCH = 2;

const int TIMER_TIME_OUT = 500;

int LISTEN_FD = 0;

void acceptConnection(int listen_fd, int epoll_fd, const string &path);

// 监听套接字
int socket_bind_listen(int port)
{
    // 检查port值，取正确区间范围
    if (port < 1024 || port > 65535)
        return -1;

    // 创建socket(ipv4 + tcp), 返回监听套接字
    int listen_fd = 0;
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    // 消除bind时 "Address already in use" 错误
    // 服务器在执行closefd（）后一般不会立即关闭而经历TIME_WAIT的过程。
    // 导致再次运行程序时显示错误Address already in use（地址被占用）。
    // 如果想重用地址，需给套接字设置相关选项。
    int optval = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
        return -1;

    // 设置服务器IP和port，以及绑定监听描述符
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((unsigned short)port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        return -1;

    // 开始监听，最大等待队列长度为 LISTENQ
    if (listen(listen_fd, LISTENQ) == -1)
        return -1;

    // 无效监听符
    if (listen_fd == -1)
    {
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

/*
    处理逻辑：
    1) 因为优先队列不支持随机访问
    2) 即使支持，随机删除某节点后破坏了堆结构，需要重新更新堆结构，
       所以对于被置为deleted的时间节点，会延迟到它超时或它前面的节点都被删除时，
       它才会被删除。一个节点被置为deleted，它最迟会在TIMER_TIME_OUT时间后删除
       这样做有两个好处
        1) 不需要遍历优先队列，省时
        2) 给超时事件一个容忍时间，就是设定的超时时间是删除的下限（并不是一道超时时间就立即删除）
           如果监听的请求在超时后的下一次请求中有一次出现了，就不用重新申请requesData节点，
           这样可以继续重复利用前面的requestData，减少一次delete和一次new时间
*/

void handler(int sig)
{
    close(LISTEN_FD);
    return;
}

int main()
{
    handle_for_sigpipe();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    if (sigaction(SIGQUIT, &sa, nullptr))
        return 0;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    if (sigaction(SIGINT, &sa, nullptr))
        return 0;

    if (Epoll::epoll_init(MAX_THREADS, LISTENQ) < 0)
    {
        perror("epoll init failed");
        return 1;
    }

    if(ThreadPool::threadpool_create(THREADPOOL_THREAD_NUM, QUEUE_SIZE) == -1)
    {
        perror("ThreadPool create failed");
        return 1;
    }
    
    LISTEN_FD = socket_bind_listen(PORT);
    if (LISTEN_FD < 0)
    {
        perror("socket init failed");
        return 1;
    }
    if (setSocketNonBlocking(LISTEN_FD) < 0)
    {
        perror("set Socket Non Blocking failed");
        return 1;
    }
    __uint32_t event = EPOLLIN | EPOLLET;
    shared_ptr<RequestData> req(new RequestData());
    req->setFd(LISTEN_FD);
    cout <<"LISTEN_FD:" << LISTEN_FD <<endl;
    if(Epoll::epoll_add(LISTEN_FD, req, event) < 0)
    {
        perror("epoll add error");
        return 1;
    }
    for (;;)
    {

        // timeout 参数详解
        // 0：函数不阻塞，不管 epoll 实例中有没有就绪的文件描述符，函数被调用后都直接返回
        // 大于 0：如果 epoll 实例中没有已就绪的文件描述符，函数阻塞对应的毫秒数再返回
        // -1：函数一直阻塞，直到 epoll 实例中有已就绪的文件描述符之后才解除阻塞
        Epoll::my_epoll_wait(LISTEN_FD, MAX_THREADS, -1);
    }
    return 0;
}