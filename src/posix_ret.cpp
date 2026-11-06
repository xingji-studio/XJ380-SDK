#include "./include/xposix/syscall_ret.h"

extern "C" long __xposix_ret(uint64_t ret)
{
    int64_t sret = (int64_t)ret;
    if (sret < 0 && sret >= -4095) {
        errno = (int)-sret;
        return -1;
    }
    return (long)sret;
}

extern "C" void *__xposix_ptr(uint64_t ret)
{
    int64_t sret = (int64_t)ret;
    if (sret < 0 && sret >= -4095) {
        errno = (int)-sret;
        return (void *)-1;
    }
    return (void *)ret;
}
