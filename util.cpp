#include "util.h"
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

ssize_t readn(int fd, void *buff, size_t n)
{
    size_t nleft = n;    // 剩余数量
    ssize_t nread = 0;   // 当前阶段已读数量
    ssize_t readSum = 0; // 已读总数
    // char* ptr = (char*)buff;
    char *ptr = static_cast<char *>(buff);

    while (nleft > 0)
    {
        nread = read(fd, ptr, nleft);
        if (nread < 0)
        {
            if (errno == EINTR) // 若是系统中断导致不算错误
                nread = 0;
            else if (errno == EAGAIN) // read函数会返回一个错误EAGAIN，
                                      // 提示你的应用程序现在没有数据可读请稍后再试
                return readSum;
            else
                return -1;
        }
        else if (nread == 0)
        { // 对等放关闭，退出
            break;
        }
        readSum += nread;
        nleft -= nread;
        ptr += nread;
    }
    return readSum;
}

ssize_t writen(int fd, void *buff, size_t n)
{
    size_t nleft = n;     // 剩余数量
    ssize_t nwritten = 0; // 当前阶段已写数量
    ssize_t writeSum = 0; // 已写总数
    char *ptr = static_cast<char *>(buff);
    while (nleft > 0)
    {
        nwritten = write(fd, ptr, nleft);
        if(nwritten < 0)
        {
            if(errno == EINTR || errno == EAGAIN)
            {
                nwritten = 0;
                continue;
            } else
                return -1;
        }
        writeSum += nwritten;
        nleft -= nwritten;
        ptr += nwritten;
    }
    return writeSum;
}

void handle_for_sigpipe()
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    // sa_handler此参数和signal()的参数handler相同，代表新的信号处理函数
    sa.sa_handler = SIG_IGN;
    // sa_flags 用来设置信号处理的其他相关操作
    sa.sa_flags = 0;
    // SIGPIPE 在reader中止之后写Pipe的时候发送
    // 定义函数 int sigaction(int signum,const struct sigaction *act ,struct sigaction *oldact);
    // 函数说明 sigaction()会依参数signum指定的信号编号来设置该信号的处理函数。
    // 参数signum可以指定SIGKILL和SIGSTOP以外的所有信号。
    if(sigaction(SIGPIPE, &sa, NULL))
        return;
}

int setSocketNonBlocking(int fd)
{
    int flag = fcntl(fd, F_GETFL, 0);
    if(flag == -1)
        return -1;
    flag |= O_NONBLOCK;
    if(fcntl(fd, F_SETFL, flag) == -1)
        return -1;
    return 0;
}

// 文件状态标志
// - F_GETFL（void）
// - F_SETFL（long）
// SIG_IGN 屏蔽该信号
// signal 返回值是上一次处理程序
