#include "./include/xposix/socket.h"
#include "./include/libsys.h"
#include "./include/xposix/syscall_ret.h"

extern "C" int socket(int domain, int type, int protocol)
{
    return (int)__xposix_ret(enter_syscall(SYS_SOCKET, domain, type, protocol, 0, 0, 0));
}

extern "C" int socketpair(int domain, int type, int protocol, int sv[2])
{
    return (int)__xposix_ret(enter_syscall(SYS_SOCKETPAIR, domain, type, protocol, (uint64_t)sv, 0, 0));
}

extern "C" int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return (int)__xposix_ret(enter_syscall(SYS_BIND, sockfd, (uint64_t)addr, addrlen, 0, 0, 0));
}

extern "C" int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return (int)__xposix_ret(enter_syscall(SYS_CONNECT, sockfd, (uint64_t)addr, addrlen, 0, 0, 0));
}

extern "C" int listen(int sockfd, int backlog)
{
    return (int)__xposix_ret(enter_syscall(SYS_LISTEN, sockfd, backlog, 0, 0, 0, 0));
}

extern "C" int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return (int)__xposix_ret(enter_syscall(SYS_ACCEPT, sockfd, (uint64_t)addr, (uint64_t)addrlen, 0, 0, 0));
}

extern "C" int shutdown(int sockfd, int how)
{
    return (int)__xposix_ret(enter_syscall(SYS_SHUTDOWN, sockfd, how, 0, 0, 0, 0));
}
