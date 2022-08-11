#ifndef __UTIL_H__
#define __UTIL_H__
#include <cstdlib>

ssize_t readn(int fd, void* buff, size_t n);
ssize_t writen(int fd, void* buff, size_t n);

void handle_for_sigpipe();
int setSocketNonBlocking(int fd); //非阻塞(non-blocking)

// ssize_t :  long int

#endif