#include "./include/sys/uio.h"
#include "./include/xposix/syscall_ret.h"
#include "./include/libsys.h"

extern "C" ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    return (ssize_t)__xposix_ret(enter_syscall(SYS_READV, fd, (uint64_t)iov, iovcnt, 0, 0, 0));
}

extern "C" ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    return (ssize_t)__xposix_ret(enter_syscall(SYS_WRITEV, fd, (uint64_t)iov, iovcnt, 0, 0, 0));
}
