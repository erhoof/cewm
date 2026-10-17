// POSIX stand-in for the few Winsock calls CERF's slirp backend makes (poll, nonblocking connect, close).
#pragma once
#include <windows.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int SOCKET;
typedef unsigned long u_long;
typedef struct pollfd WSAPOLLFD;
typedef struct { int dummy; } WSADATA;
#define INVALID_SOCKET (-1)
#define MAKEWORD(a, b) (((a) & 0xff) | (((b) & 0xff) << 8))
#define WSAEWOULDBLOCK EINPROGRESS
static inline int WSAStartup(unsigned short, WSADATA*) { return 0; }
static inline int WSAGetLastError() { return errno; }
static inline int WSAPoll(WSAPOLLFD* f, unsigned long n, int ms) { return poll(f, (nfds_t)n, ms); }
static inline int closesocket(SOCKET s) { return close(s); }
static inline int ioctlsocket(SOCKET s, long cmd, u_long* arg) { int v = (int)*arg; return ioctl(s, (unsigned long)cmd, &v); }
#define getsockopt(s, l, o, v, len) getsockopt(s, l, o, v, reinterpret_cast<socklen_t*>(len))
