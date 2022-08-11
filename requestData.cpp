#include "requestData.h"
#include "util.h"
#include "epoll.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/time.h>
#include <unordered_map>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <queue>
#include <iostream>
#include <opencv/cv.h>
#include <stdio.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
using namespace std;
using namespace cv;

// test
mutex qlock;
once_flag MimeType::once_control;
unordered_map<string, string> MimeType::mime;

void MimeType::init()
{
    mime[".html"] = "text/html";
    mime[".avi"] = "video/x-msvideo";
    mime[".bmp"] = "image/bmp";
    mime[".c"] = "text/plain";
    mime[".doc"] = "application/msword";
    mime[".gif"] = "image/gif";
    mime[".gz"] = "application/x-gzip";
    mime[".htm"] = "text/html";
    mime[".ico"] = "application/x-ico";
    mime[".jpg"] = "image/jpeg";
    mime[".png"] = "image/png";
    mime[".txt"] = "text/plain";
    mime[".mp3"] = "audio/mp3";
    mime["default"] = "text/html";
}

string MimeType::getMime(const string &suffix)
{
    // 双重锁定 初始化
    call_once(once_control, init); 
    if (mime.count(suffix) != 0)
        return mime[suffix];
    else
        return mime["default"];
}

RequestData::RequestData() : now_read_pos(0),
                             state(STATE_PARSE_URI),
                             h_state(h_start),
                             keep_alive(false),
                             againTimes(0)
{
    // cout << "RequestData constructed!" << endl;
}

RequestData::RequestData(int _epollfd, int _fd, string _path) : now_read_pos(0),
                                                                state(STATE_PARSE_URI),
                                                                h_state(h_start),
                                                                keep_alive(false),
                                                                againTimes(0),
                                                                path(_path),
                                                                fd(_fd),
                                                                epollfd(_epollfd)
{
}

RequestData::~RequestData()
{
    // cout << "~RequestData" << endl;
    // Epoll::epoll_del(fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
    // // if (timer.lock())
    // if (!timer.expired()) // 非空
    // {
    //     // shared_ptr<mytimer> my_timer(timer.lock());
    //     // my_timer->clearReq();
    //     timer.lock()->clearReq();
    //     timer.reset();
    // }
    close(fd);
}

void RequestData::linkTimer(shared_ptr<TimerNode> mtimer)
{
    // shared_ptr重载了bool, 但weak_ptr没有
    // if(!timer.lock()) // 等价于 if(!timer.expired()) // timer 没有被销毁
    timer = mtimer;
    // cout << "addTimer success" << endl;
}

int RequestData::getFd()
{
    return fd;
}

void RequestData::setFd(int mfd)
{
    fd = mfd;
}

void RequestData::reset()
{
    againTimes = 0;
    content.clear();
    file_name.clear();
    path.clear();
    now_read_pos = 0;
    state = STATE_PARSE_URI;
    h_state = h_start;
    headers.clear();
    keep_alive = false;
    // cout <<"reset:" <<endl;
    if(timer.lock())
    {
        timer.lock() -> clearReq();
        timer.reset();
    }
}

void RequestData::seperateTimer()
{
    // if(timer.use_count() == 0) {
    //     perror("seperateTimer errror");
    // }
    if (!timer.expired())
    {
        // shared_ptr<mytimer> my_timer(timer.lock());
        // my_timer->clearReq();
        timer.lock()->clearReq();
        timer.reset();
    }
}


void RequestData::handleRequest()
{
    char buff[MAX_BUFF];
    bool isError = false;
    while (true)
    {
        int read_num = readn(fd, buff, MAX_BUFF);
        if (read_num < 0)
        {
            perror("1");
            isError = true;
            break;
        }
        else if (read_num == 0)
        {
            // 有请求出现但是读不到数据，可能是Request Aborted
            // 或者来自网络的数据没有到达等原因
            // 对于这样的请求尝试超时一定次数就抛弃
            // perror("read_num == 0");
            if (errno == EAGAIN)
            {
                if (againTimes > AGAIN_MAX_TIMES)
                    isError = true;
                else
                    ++againTimes;
            }
            else if (errno != 0)
                isError = true;
            break;
        }
        string now_read(buff, buff + read_num);
        content += now_read;

        if (state == STATE_PARSE_URI)
        {
            int flag = this->parse_URI();
            if (flag == PARSE_URI_AGAIN)
                break;
            else if (flag == PARSE_URI_ERROR)
            {
                perror("2");
                isError = true;
                break;
            }
        }
        if (state == STATE_PARSE_HEADERS)
        {
            int flag = this->parse_Headers();
            if (flag == PARSE_HEADER_AGAIN)
                break;
            else if (flag == PARSE_HEADER_ERROR)
            {
                perror("3");
                isError = true;
                break;
            }
            if (method == METHOD_POST)
                state = STATE_RECV_BODY;
            else
                state = STATE_ANALYSIS;
        }
        if (state == STATE_RECV_BODY)
        {
            int content_length = -1;
            if (headers.find("Content-length") != headers.end())
            {
                content_length = stoi(headers["Content-length"]);
            }
            else
            {
                isError = true;
                break;
            }
            if (content.size() < content_length)
                continue;
            state = STATE_ANALYSIS;
        }
        if (state == STATE_ANALYSIS)
        {
            int flag = this->analysisRequest();
            if (flag == ANALYSIS_SUCCESS)
            {
                state = STATE_FINISH;
                break;
            }
            else
            {
                isError = true;
                break;
            }
        }
    }
    // cout <<"handleRequest: isError:" << isError <<", state:" << state <<", keep_alive:" << keep_alive <<endl;
    if (isError)
        return;
    // 加入 epoll 继续
    if (state == STATE_FINISH)
    {
        if (keep_alive)
        {
            // printf("ok keep-alive\n");
            this->reset();
        }
        else
        {
            return;
        }
    }

    // ??????????????????
    // 一定要先加入信息，否则可能会出现刚加进去，下个in触发来了，然后分离失败后，又加入队列。
    // 最后超时被删，然后正在线程中进行任务出错，double free 错误
    // 新增时间信息
    // pthread_mutex_lock(&qlock);
    Epoll::add_timer(shared_from_this(), 500);

    // pthread_mutex_unlock(&qlock);

    __uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
    // EPOLLIN 触发情况
    // 1. 有新的连接请求
    // 2. 接收到普通数据（且接收缓冲区没满）
    // 3. 客户端正常关闭
    // EPOLLOUT 触发情况
    // 只要发送缓冲区未满，就会触发
    int ret = Epoll::epoll_mod(fd, shared_from_this(), _epo_event);
    if (ret == -1)
    {
        // 返回错误处理
        // delete this;
        return;
    }
}

int RequestData::parse_URI()
{
    string &str = content;
    // 读到完整的请求行在开始解析请求

    // POST /mmtls/00007137 HTTP/1.1\r\n
    int pos = str.find('\r', now_read_pos);
    // 请求行：method SP URI SP Version CRLF
    // if not find, return -1
    if (pos < 0)
        return PARSE_URI_AGAIN;

    // 去掉请求行所占空间，节省空间
    string request_line = str.substr(0, pos);
    // 头部字段由：请求行（状态行）、请求头（响应头）、空行 组成

    // 如果 '\r' 是最后一个字符，则表示没有请求头，清空str(即content，节省空间。)
    // 请求头数据在request_line中已有一份
    // 否则表示有请求体，更新str为请求体内容。因为请求行和请求头之间有CRLF
    // pos 代表空行'\r'位置，所以是 pos + 1 到 end

    if (str.size() > pos + 1)
        str = str.substr(pos + 1);
    else
        str.clear();

    // Method
    pos = request_line.find("GET");
    if (pos < 0)
    {
        pos = request_line.find("POST");
        if (pos < 0)
            return PARSE_URI_ERROR;
        else
            method = METHOD_POST;
    }
    else
        method = METHOD_GET;

    // printf("method = %d\n", method);

    // filename
    // 请求行：method SP URI SP Version CRLF
    // filename = URI
    // eg: GET /18-1 HTTP/1.1
    // Prequest_line:POST /mmtls/00007137 HTTP/1.1
    pos = request_line.find("/", pos);
    if (pos < 0)
    {
        return PARSE_URI_ERROR;
    }
    int _pos = request_line.find(' ', pos);

    if (_pos < 0)
        return PARSE_URI_ERROR;

    if (_pos - pos > 1)
    {
        file_name = request_line.substr(pos + 1, _pos - pos - 1);
        int __pos = file_name.find('?');
        if (__pos >= 0)
        {
            file_name = file_name.substr(0, __pos);
        }
    }
    else
        file_name = "index.html";
    pos = _pos;

    // cout << "file_name: " << file_name << endl;
    // HTTP 版本号
    pos = request_line.find("/", pos);
    if (pos < 0)
        return PARSE_URI_ERROR;

    if (request_line.size() - pos <= 3)
        return PARSE_URI_ERROR;

    string ver = request_line.substr(pos + 1, 3);
    if (ver == "1.0")
        HTTPversion = HTTP_10;
    else if (ver == "1.1")
        HTTPversion = HTTP_11;
    else
        return PARSE_URI_ERROR;

    state = STATE_PARSE_HEADERS;
    // cout << "parse_URI content:" << content << ", str:" << str << endl;
    return PARSE_URI_SUCCESS;
}

int RequestData::parse_Headers()
{
    // Fileld name: Field Value CRLF
    // Fileld name: Field Value CRLF
    // CRLF
    string &str = content;
    int key_start = -1, key_end = -1, value_start = -1, value_end = -1;
    int now_read_line_begin = 0;
    bool notFinish = true;
    for (int i = 0; i < str.size() && notFinish; ++i)
    {
        //Accept: */*\r\n
        //Host: www.chrono.com\r\n
        //Cache-Control: no-cache\r\n
        //\r\n
        //BODY

        switch (h_state)
        {
        case h_start:
        {
            if (str[i] == '\n' || str[i] == '\r')
                break;
            h_state = h_key;
            key_start = i;
            now_read_line_begin = i;
            break;
        }
        case h_key:
        {
            if (str[i] == ':')
            {
                key_end = i;
                if (key_end - key_start <= 0)
                    return PARSE_HEADER_ERROR;
                h_state = h_colon;
            }
            else if (str[i] == '\n' || str[i] == '\r')
                return PARSE_HEADER_ERROR;
            break;
        }
        case h_colon:
        {
            if (str[i] == ' ')
            {
                h_state = h_spaces_after_colon;
                // while (i + 1 < str.size() && str[i + 1] == ' ')
                //     ++i;
            }
            else
                return PARSE_HEADER_ERROR;
            break;
        }
        case h_spaces_after_colon:
        {
            h_state = h_value;
            value_start = i;
            break;
        }
        case h_value:
        {
            if (str[i] == '\r')
            {
                h_state = h_CR;
                value_end = i;
                if (value_end - value_start <= 0)
                    return PARSE_HEADER_ERROR;
            }
            else if (i - value_start > 255)
                return PARSE_HEADER_ERROR;
            break;
        }
        case h_CR:
        {
            if (str[i] == '\n')
            {
                h_state = h_LF;
                string key(str.begin() + key_start, str.begin() + key_end);
                string value(str.begin() + value_start, str.begin() + value_end);
                headers[key] = value;
                now_read_line_begin = i;
            }
            else
                return PARSE_HEADER_ERROR;
            break;
        }
        case h_LF:
        {
            if (str[i] == '\r')
            {
                h_state = h_end_CR;
            }
            else
            {
                key_start = i;
                h_state = h_key;
            }
            break;
        }
        case h_end_CR:
        {
            if (str[i] == '\n')
                h_state = h_end_LF;
            else
                return PARSE_HEADER_ERROR;
            break;
        }
        case h_end_LF:
        {
            notFinish = false;
            now_read_line_begin = i;
            break;
        }
        default:
            break;
        }
    }
    str = str.substr(now_read_line_begin);
    if (h_state == h_end_LF)
        return PARSE_HEADER_SUCCESS;
    return PARSE_HEADER_AGAIN;
}

int RequestData::analysisRequest()
{
    if (method == METHOD_POST)
    {
        // GET content
        char header[MAX_BUFF];
        sprintf(header, "HTTP/1.1 %d %s\r\n", 200, "OK");
        if (headers.find("Connection") != headers.end() && headers["Connection"] == "keep-alive")
        {
            keep_alive = true;
            sprintf(header, "%sConnection: Keep-alive\r\n", header);
            sprintf(header, "%sKeep-Alive: timeout=%d\r\n", header, EPOLL_WAIT_TIME);
        }
        cout << "analysisRequest content:" << content << endl;
        // test char*
        char send_content[] = "I have receiced this.";

        sprintf(header, "%sContent-length: %zu\r\n", header, strlen(send_content));
        sprintf(header, "%s\r\n", header); // 写入 \r\n 标志结束
        size_t send_len = (size_t)writen(fd, header, strlen(header));
        if (send_len != strlen(header))
        {
            perror("Send header failed!");
            return ANALYSIS_ERROR;
        }

        send_len = (size_t)writen(fd, send_content, strlen(send_content));
        if (send_len != strlen(send_content))
        {
            perror("Send connect failed!");
            return ANALYSIS_ERROR;
        }
        // cout << "content size = " << content.size() << endl;
        // vector<char> data(content.begin(), content.end());
        //////////////////////////////////////
        // Mat test = imdecode(data, CV_LOAD_IMAGE_ANYDEPTH | CV_LOAD_IMAGE_ANYCOLOR);
        // imwrite("receive.bmp", test);
        /////////////////////////////////////
        return ANALYSIS_SUCCESS;
    }
    else if (method == METHOD_GET)
    {
        char header[MAX_BUFF];
        sprintf(header, "HTTP/1.1 %d %s\r\n", 200, "OK");
        if (headers.find("Connection") != headers.end() && headers["Connection"] == "keep-alive")
        {
            keep_alive = true;
            sprintf(header, "%sConnection: Keep-alive\r\n", header);
            sprintf(header, "%sKeep-Alive: timeout=%d\r\n", header, EPOLL_WAIT_TIME);
        }

        int dot_pos = file_name.find('.');
        string filetype;
        if (dot_pos < 0)
            filetype = MimeType::getMime("default");
        else
            filetype = MimeType::getMime(file_name.substr(dot_pos));

        struct stat sbuf;
        if (stat(file_name.c_str(), &sbuf) < 0)
        {
            handleError(fd, 404, "Not Found!");
            return ANALYSIS_ERROR;
        }

        sprintf(header, "%sContent-type: %s\r\n", header, filetype);
        // 通过 Content-length 返回文件大小
        sprintf(header, "%sContent-length: %ld\r\n", header, sbuf.st_size);
        sprintf(header, "%s\r\n", header);

        size_t send_len = (size_t)writen(fd, header, strlen(header));
        if (send_len != strlen(header))
        {
            perror("Send header failed!");
            return ANALYSIS_ERROR;
        }
        int src_fd = open(file_name.c_str(), O_RDONLY, 0);
        // mmap函数可以把文件映射到进程的虚拟内存空间。
        // 通过对这段内存的读取和修改，可以实现对文件的读取和修改
        // 而不需要用read和write函数。
        char *src_addr = static_cast<char *>(mmap(NULL, sbuf.st_size, PROT_READ,
                                                  MAP_PRIVATE, src_fd, 0));
        close(src_fd);

        // 发送文件并检验完整性
        send_len = (size_t)writen(fd, src_addr, sbuf.st_size);
        if (send_len != sbuf.st_size)
        {
            perror("Send file failed");
            munmap(src_addr, sbuf.st_size);
            return ANALYSIS_ERROR;
        }

        //与mmap函数成对使用的是munmap函数，它是用来解除映射的函数
        munmap(src_addr, sbuf.st_size);
        return ANALYSIS_SUCCESS;
    }
    return ANALYSIS_ERROR;
}

// handleError(fd, 404, "Not Found!");
void RequestData::handleError(int fd, int err_num, string short_msg)
{
    short_msg = " " + short_msg;
    char send_buff[MAX_BUFF];
    string body_buff, header_buff;
    body_buff += "<html><title>TKeed Error</title>";
    body_buff += "<body bgcolor=\"ffffff\">";
    body_buff += to_string(err_num) + short_msg;
    body_buff += "<hr><em> Z's Web Server</em>\n</body></html>";

    header_buff += "HTTP/1.1 " + to_string(err_num) + short_msg + "\r\n";
    header_buff += "Content-type: text/html\r\n";
    header_buff += "Connection: close\r\n";
    header_buff += "Content-length: " + to_string(body_buff.size()) + "\r\n";
    header_buff += "\r\n";

    sprintf(send_buff, "%s", header_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
    sprintf(send_buff, "%s", body_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
}
