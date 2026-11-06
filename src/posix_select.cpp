#include "./include/sys/select.h"
#include "./include/xposix/syscall_ret.h"
#include "./include/libsys.h"

struct xj_pselect6_arg {
    const void *ss;
    size_t ss_len;
};

extern "C" int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    return (int)__xposix_ret(enter_syscall(SYS_SELECT, nfds, (uint64_t)readfds, (uint64_t)writefds,
                                           (uint64_t)exceptfds, (uint64_t)timeout, 0));
}

extern "C" int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                       const struct timespec *timeout, const void *sigmask)
{
    xj_pselect6_arg arg = {sigmask, sigmask ? sizeof(uint64_t) : 0};
    return (int)__xposix_ret(enter_syscall(SYS_PSELECT6, nfds, (uint64_t)readfds, (uint64_t)writefds,
                                           (uint64_t)exceptfds, (uint64_t)timeout, (uint64_t)&arg));
}
