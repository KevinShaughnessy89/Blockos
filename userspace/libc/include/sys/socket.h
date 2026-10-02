#pragma once
#include <stddef.h>
#include <stdint.h>
typedef long ssize_t;

typedef unsigned int socklen_t;
typedef uint16_t sa_family_t;

struct sockaddr { sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_un { sa_family_t sun_family; char sun_path[108]; };
struct iovec { void* iov_base; size_t iov_len; };
struct msghdr {
    void* msg_name;
    socklen_t msg_namelen;
    struct iovec* msg_iov;
    size_t msg_iovlen;
    void* msg_control;
    size_t msg_controllen;
    int msg_flags;
};

#define AF_UNIX 1
#define AF_LOCAL AF_UNIX
#define AF_INET 2
#define AF_INET6 10
#define IPPROTO_IP 0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
struct in_addr { uint32_t s_addr; };
struct sockaddr_in { sa_family_t sin_family; uint16_t sin_port; struct in_addr sin_addr; uint8_t sin_zero[8]; };
struct in6_addr { uint8_t s6_addr[16]; };
struct sockaddr_in6 { sa_family_t sin6_family; uint16_t sin6_port; uint32_t sin6_flowinfo; struct in6_addr sin6_addr; uint32_t sin6_scope_id; };
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_CLOEXEC 0x80000
#define SOCK_NONBLOCK 0x800
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
#define SOL_SOCKET 1
#define SO_ERROR 4

int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int sv[2]);
int bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
int accept4(int sockfd, struct sockaddr* addr, socklen_t* addrlen, int flags);
int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
ssize_t send(int sockfd, const void* buf, size_t len, int flags);
ssize_t recv(int sockfd, void* buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src, socklen_t* addrlen);
ssize_t sendmsg(int sockfd, const struct msghdr* msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr* msg, int flags);
int shutdown(int sockfd, int how);
int setsockopt(int sockfd,int level,int optname,const void* optval,socklen_t optlen);
int getsockopt(int sockfd,int level,int optname,void* optval,socklen_t* optlen);
