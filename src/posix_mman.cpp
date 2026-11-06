#include "./include/sys/mman.h"
#include "./include/xposix/syscall_ret.h"
#include "./include/libsys.h"
#include "./include/xposix/stdarg.h"

extern "C" void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    return __xposix_ptr(enter_syscall(SYS_MMAP, (uint64_t)addr, length, prot, flags, fd, (uint64_t)offset));
}

extern "C" int munmap(void *addr, size_t length)
{
    return (int)__xposix_ret(enter_syscall(SYS_MUNMAP, (uint64_t)addr, length, 0, 0, 0, 0));
}

extern "C" int mprotect(void *addr, size_t length, int prot)
{
    return (int)__xposix_ret(enter_syscall(SYS_MPROTECT, (uint64_t)addr, length, prot, 0, 0, 0));
}

extern "C" void *mremap(void *old_addr, size_t old_size, size_t new_size, int flags, ...)
{
    void *new_addr = NULL;
    if (flags & MREMAP_FIXED) {
        va_list ap;
        va_start(ap, flags);
        new_addr = va_arg(ap, void *);
        va_end(ap);
    }
    return __xposix_ptr(enter_syscall(SYS_MREMAP, (uint64_t)old_addr, old_size, new_size, flags, (uint64_t)new_addr, 0));
}

extern "C" int mincore(void *addr, size_t length, unsigned char *vec)
{
    return (int)__xposix_ret(enter_syscall(SYS_MINCORE, (uint64_t)addr, length, (uint64_t)vec, 0, 0, 0));
}
