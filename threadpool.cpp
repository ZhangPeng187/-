#include "threadpool.h"

std::mutex ThreadPool::lock;
std::condition_variable ThreadPool::notify;    // 通知工作线程的条件变量
std::vector<std::thread> ThreadPool::threads;  // 包含工作线程ID的数组
std::vector<ThreadPoolTask> ThreadPool::queue; // 包含任务队列的数组
int ThreadPool::thread_count = 0;              // 线程数目
int ThreadPool::queue_size = 0;                // 任务队列的大小
int ThreadPool::head = 0;                      // 第一个元素的索引
int ThreadPool::tail = 0;                      // 下一个元素的索引
int ThreadPool::count = 0;                     // 待处理任务数
int ThreadPool::shutdown = 0;                  // 线程池关闭标志
int ThreadPool::started = 0;                   // 开始线程数目

const int ThreadPool::THREADPOOL_THREAD_NUM = 10;
const int ThreadPool::QUEUE_SIZE = 1024;

int ThreadPool::threadpool_create(int _thread_count, int _queue_size)
{
    bool err = false;
    do
    {
        if (_thread_count <= 0 || _thread_count > MAX_THREADS || _queue_size <= 0 || _queue_size > MAX_QUEUE)
        {
            _thread_count = ThreadPool::THREADPOOL_THREAD_NUM;
            _queue_size = ThreadPool::QUEUE_SIZE;
        }

        // 初始化
        thread_count = 0;
        queue_size = _queue_size;
        head = tail = count = 0;
        shutdown = started = 0;

        threads.resize(_thread_count);
        queue.resize(_queue_size);

        // 启动工作线程
        for (int i = 0; i < _thread_count; ++i)
        {
            threads[i] = std::thread(threadpool_handle, nullptr);
            thread_count++;
            started++;
        }
    } while (false);

    if (err)
        return -1;

    return 0;
}

void myHandler(std::shared_ptr<void> req)
{
    std::shared_ptr<RequestData> request = std::static_pointer_cast<RequestData>(req);
    request->handleRequest();
}

int ThreadPool::threadpool_add(std::shared_ptr<void> args)
{
    int err = 0, next = 0;
    std::function<void(std::shared_ptr<void>)> fun = myHandler;
    std::unique_lock<std::mutex> uqlock(lock);

    next = (tail + 1) % queue_size;
    do
    {
        // 线程池是否满了
        if (count == queue_size)
        {
            err = THREADPOOL_QUEUE_FULL;
            break;
        }

        // 已关闭
        if (shutdown)
        {
            err = THREADPOOL_SHUTDOWN;
            break;
        }

        // 向任务队列添加任务
        queue[tail].fun = fun;
        queue[tail].args = args;
        tail = next;
        count += 1;
        notify.notify_one();
    } while (false);

    return err;
}

int ThreadPool::threadpool_destory(ShutDownOption shutdown_option)
{
    printf("Thread pool destroy !\n");
    std::unique_lock<std::mutex> uqlock(lock);
    int err = 0;
    do
    {
        if (shutdown)
        {
            err = THREADPOOL_SHUTDOWN;
            break;
        }
        shutdown = shutdown_option;
        notify.notify_all();
        uqlock.unlock();
        for(int i = 0; i < thread_count; ++i)
            threads[i].join();
    } while (false);

    if(!err) {
        threadpool_free();
    }
    return err;
}

int ThreadPool::threadpool_free()
{
    if(started > 0)
        return -1;
    return 0;
}

void ThreadPool::threadpool_handle(void *args)
{
    std::unique_lock<std::mutex> uqlock;

    for (;;)
    {
        ThreadPoolTask task;
        // 锁必须等待条件变量
        uqlock = std::unique_lock<std::mutex>(lock);
        // 等待条件变量，检查虚假唤醒
        // 当从pthread_cond_wait()返回时，我们拥有锁
        notify.wait(uqlock, [&]() -> bool
                    {
                        if (!count && !shutdown)
                            return false;
                        return true;
                    });

        // while ((count == 0) && (!shutdown))
        // pthread_cond_wait(&(notify), &(lock));

        if ((shutdown == immediate_shutdown) ||
            ((shutdown == graceful_shutdown && (count) == 0)))
            break;

        // 捕获任务
        task.fun = queue[head].fun;
        task.args = queue[head].args;
        queue[head].fun = nullptr;
        queue[head].args.reset();
        head = (head + 1) % queue_size;
        count -= 1;
        uqlock.unlock();
        // 执行任务
        task.fun(task.args);
    }
    --started;
    uqlock.unlock();
    printf("This threadpool thread %d finishs!\n", std::this_thread::get_id());
    // pthread_exit(nullptr);
    return;
}