#include "./include/sys/ioctl.h"
#include "./include/xposix/syscall_ret.h"
#include "./include/libsys.h"
#include "./include/xposix/stdarg.h"

extern "C" int ioctl(int fd, unsigned long request, ...)
{
    void *arg = NULL;
    va_list ap;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    return (int)__xposix_ret(enter_syscall(SYS_IOCTL, fd, request, (uint64_t)arg, 0, 0, 0));
}
