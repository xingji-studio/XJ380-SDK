#include "./include/time.h"
#include "./include/sys/time.h"
#include "./include/xposix/syscall_ret.h"
#include "./include/libsys.h"

extern "C" int clock_gettime(int clockid, struct timespec *ts)
{
    return (int)__xposix_ret(enter_syscall(SYS_CLOCK_GETTIME, clockid, (uint64_t)ts, 0, 0, 0, 0));
}

extern "C" int clock_getres(int clockid, struct timespec *ts)
{
    return (int)__xposix_ret(enter_syscall(SYS_CLOCK_GETRES, clockid, (uint64_t)ts, 0, 0, 0, 0));
}

extern "C" int nanosleep(const struct timespec *req, struct timespec *rem)
{
    (void)rem;
    return (int)__xposix_ret(enter_syscall(SYS_NANOSLEEP, (uint64_t)req, 0, 0, 0, 0, 0));
}

extern "C" int clock_nanosleep(int clockid, int flags, const struct timespec *req, struct timespec *rem)
{
    return (int)__xposix_ret(enter_syscall(SYS_CLOCK_NANOSLEEP, clockid, flags, (uint64_t)req, (uint64_t)rem, 0, 0));
}

extern "C" int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv == NULL) {
        errno = EINVAL;
        return -1;
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) return -1;
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;
    return 0;
}
