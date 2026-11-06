#include "./include/xposix/poll.h"
#include "./include/libsys.h"
#include "./include/xposix/syscall_ret.h"

extern "C" int poll(struct pollfd *fds, nfds_t nfds, int timeout_ms)
{
    return (int)__xposix_ret(enter_syscall(SYS_POLL, (uint64_t)fds, nfds, (uint64_t)timeout_ms, 0, 0, 0));
}
