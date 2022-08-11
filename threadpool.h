#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__
#include "requestData.h"
#include <thread>
#include <mutex>
#include <functional>
#include <vector>
#include <iostream>
#include <queue>
#include <condition_variable>

const int THREADPOOL_INVALID = -1;
const int THREADPOOL_LOCK_FAILURE = -2;
const int THREADPOOL_QUEUE_FULL = -3;
const int THREADPOOL_SHUTDOWN = -4;
const int THREADPOOL_THREAD_FAILURE = -5;
const int THREADPOOL_GRACEFUL = 1;

const int MAX_THREADS = 1024;
const int MAX_QUEUE = 65535;

typedef enum
{
    immediate_shutdown = 1,
    graceful_shutdown = 2
} ShutDownOption;

struct ThreadPoolTask
{
    std::function<void(std::shared_ptr<void>)> fun; // 任务回调函数
    std::shared_ptr<void> args;           // 回调函数参数
};


void myHandler(std::shared_ptr<void> req);

class ThreadPool
{
private:
    static std::mutex lock;
    static std::condition_variable notify;  // 通知工作线程的条件变量
    static std::vector<std::thread> threads;  // 包含工作线程ID的数组
    static std::vector<ThreadPoolTask> queue; // 包含任务队列的数组
    static int thread_count;         // 线程数目
    static int queue_size;           // 任务队列的大小
    static int head;                 // 第一个元素的索引
    static int tail;                 // 下一个元素的索引
    static int count;                // 待处理任务数
    static int shutdown;             // 线程池关闭标志
    static int started;              // 开始线程数目
    static const int THREADPOOL_THREAD_NUM;
    static const int QUEUE_SIZE;
public:
    static int threadpool_create(int thread_count, int queue_size);
    static int threadpool_add(std::shared_ptr<void> args);
    static int threadpool_destory(ShutDownOption shutdown_option = graceful_shutdown);
    static int threadpool_free();
    static void threadpool_handle(void *args);
};




#endif