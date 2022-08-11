#ifndef __TIMER_H__
#define __TIMER_H__
#include "requestData.h"
#include "./base/nocopyable.hpp"
// #include "./base/mutexLock.hpp"
#include <unistd.h>
#include <memory>
#include <queue>
#include <mutex>
#include <deque>

class RequestData;

class TimerNode
{
    typedef std::shared_ptr<RequestData> SP_ReqData;

private:
    bool deleted;
    size_t expired_time; // 过期的时间
    SP_ReqData request_data;

public:
    TimerNode(SP_ReqData request_data, int timeout);
    ~TimerNode();
    void update(int timeout);
    bool isvalid();
    void clearReq();
    void setDeleted();
    bool isDeleted() const;
    size_t getExpTime() const;
};

struct timerCmp
{
    bool operator()(std::shared_ptr<TimerNode> &a, std::shared_ptr<TimerNode> &b) const
    {
        return a->getExpTime() > b->getExpTime();
    }
};

class TimerManager
{
    typedef std::shared_ptr<RequestData> SP_ReqData;
    typedef std::shared_ptr<TimerNode> SP_TimerNode;

private:
    std::priority_queue<SP_TimerNode, std::deque<SP_TimerNode>, timerCmp> TimerNodeQueue;
    std::mutex lock;
public:
    TimerManager();
    ~TimerManager();
    void addTimer(SP_ReqData request_data, int timeout);
    void addTimer(SP_TimerNode timer_node);
    void handle_expired_event();
};

#endif