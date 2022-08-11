#ifndef __REQUESTDATA_H__
#define __REQUESTDATA_H__
#include <string>
#include <unordered_map>
#include <mutex>
#include "timer.h"
#include <memory>

const int STATE_PARSE_URI = 1;
const int STATE_PARSE_HEADERS = 2;
const int STATE_RECV_BODY = 3;
const int STATE_ANALYSIS = 4;
const int STATE_FINISH = 5;

const int MAX_BUFF = 4096;

// 有请求出现但是读不到数据，可能是Request Aborted
// 或者来自网络的数据没有到达等原因
// 对这样的请求尝试超过一定次数就抛弃
const int AGAIN_MAX_TIMES = 200;

const int PARSE_URI_AGAIN = -1;
const int PARSE_URI_ERROR = -2;
const int PARSE_URI_SUCCESS = 0;

const int PARSE_HEADER_AGAIN = -1;
const int PARSE_HEADER_ERROR = -2;
const int PARSE_HEADER_SUCCESS = 0;

const int ANALYSIS_ERROR = -2;
const int ANALYSIS_SUCCESS = 0;

const int METHOD_POST = 1;
const int METHOD_GET = 2;
const int HTTP_10 = 1;
const int HTTP_11 = 2;

const int EPOLL_WAIT_TIME = 500;

class MimeType
{
private:
    // static pthread_mutex_t lock;
    static void init();
    static std::unordered_map<std::string, std::string> mime;
    MimeType();
    MimeType(const MimeType &m);

public:
    static std::string getMime(const std::string &suffix);

private:
    static std::once_flag once_control;
};

enum HeadersState
{
    h_start = 0,
    h_key,
    h_colon, // 冒号
    h_spaces_after_colon,
    h_value,
    h_CR,
    h_LF,
    h_end_CR,
    h_end_LF
};

/*
    OCTET          = 任意八字节的序列
    CHAR           = 任意US-ASCII中的字符 0-127
    UPALPHA        = 任意US-ASCII中的大写字母
    LOALPHA        = 任意US-ASCII中的小写字母
    ALPHA          = 任意US-ASCII中的大小写字母
    DIGIT          = 任意US-ASCII中的数字
    CTL            = 任意US-ASCII中的控制符
                    (八字节 0 - 31) 和 DEL (ASCII 127)>
    HT             = Tab键, horizontal-tab (ASCII 9)
    <">            = 双引号 （ASCII 34）
    CR             = 回车 （ASCII 13）
    LF             = 换行 （ASCII 10）
    SP             = 空格 （ASCII 32）
*/

struct TimerNode;
class RequestData;

class RequestData : public std::enable_shared_from_this<RequestData>
{
private:
    int againTimes;
    std::string path;
    int fd;
    int epollfd;
    // content 内容用完就清除
    std::string content;
    int method;
    int HTTPversion;
    std::string file_name;
    int now_read_pos;
    int state;
    int h_state;
    bool isfinish;
    bool keep_alive;
    std::unordered_map<std::string, std::string> headers;
    std::weak_ptr<TimerNode> timer;

private:
    int parse_URI();
    int parse_Headers();
    int analysisRequest();

public:
    RequestData();
    RequestData(int epollfd, int fd, std::string);
    ~RequestData();
    void linkTimer(std::shared_ptr<TimerNode> mtimer);
    void reset();
    void seperateTimer();
    int getFd();
    void setFd(int fd);
    void handleRequest();
    void handleError(int fd, int err_num, std::string msg);
};

#endif