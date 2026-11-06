#include "./include/xposix.h"
#include "./include/xpermissions.h"
#include "./include/xtuiapi.h"
#include "./include/libsys.h"
#include "./include/xposix/syscall_ret.h"

extern "C" pid_t fork()
{
    return (pid_t)__xposix_ret(enter_syscall(SYS_FORK, 0, 0, 0, 0, 0, 0));
}

extern "C" pid_t vfork()
{
    return (pid_t)__xposix_ret(enter_syscall(SYS_VFORK, 0, 0, 0, 0, 0, 0));
}

extern "C" int wait(int *status)
{
    return (int)__xposix_ret(enter_syscall(SYS_WAIT4, (uint64_t)-1, (uint64_t)status, 0, 0, 0, 0));
}

extern "C" int waitpid(int pid, int *status, int options)
{
    return (int)__xposix_ret(enter_syscall(SYS_WAIT4, (uint64_t)pid, (uint64_t)status, (uint64_t)options, 0, 0, 0));
}

extern "C" int close(int fd)
{
    return (int)__xposix_ret(enter_syscall(SYS_CLOSE, fd, 0, 0, 0, 0, 0));
}

extern "C" int fcntl(int fd, int cmd, uint64_t arg)
{
    return (int)__xposix_ret(enter_syscall(SYS_FCNTL, fd, cmd, arg, 0, 0, 0));
}

extern "C" int execve(const char *filename, char *const argv[], char *const envp[])
{
    bool terminal = filename && !strcmp(filename, "/apps/system/shell/shell.elf") && argv && argv[0] &&
                    !strcmp(argv[0], "-xj380sys-terminal-app-mode");
    if (!terminal && xj380_auto_file_access(filename, XFA_READ) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_EXECVE, (uint64_t)filename, (uint64_t)argv, (uint64_t)envp, 0, 0, 0));
}

extern "C" int chdir(const char *path)
{
    if (xj380_auto_file_access(path, XFA_LIST) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_CHDIR, (uint64_t)path, 0, 0, 0, 0, 0));
}

extern "C" int fchdir(int fd)
{
    return (int)__xposix_ret(enter_syscall(SYS_FCHDIR, fd, 0, 0, 0, 0, 0));
}

extern "C" char *getcwd(char *buffer, size_t length)
{
    if (buffer == NULL || length == 0) return NULL;
    if (__xposix_ret(enter_syscall(SYS_GETCWD, (uint64_t)buffer, length, 0, 0, 0, 0)) < 0) return NULL;
    return buffer;
}

extern "C" pid_t getpid(void)
{
    return (pid_t)__xposix_ret(enter_syscall(SYS_GETPID, 0, 0, 0, 0, 0, 0));
}

extern "C" pid_t getppid(void)
{
    return (pid_t)__xposix_ret(enter_syscall(SYS_GETPPID, 0, 0, 0, 0, 0, 0));
}

extern "C" pid_t setsid(void)
{
    return (pid_t)__xposix_ret(enter_syscall(SYS_SETSID, 0, 0, 0, 0, 0, 0));
}

extern "C" pid_t getsid(pid_t pid)
{
    return (pid_t)__xposix_ret(enter_syscall(SYS_GETSID, pid, 0, 0, 0, 0, 0));
}

extern "C" pid_t gettid(void)
{
    return (pid_t)__xposix_ret(enter_syscall(SYS_GETTID, 0, 0, 0, 0, 0, 0));
}

extern "C" gid_t getgid(void)
{
    return (gid_t)__xposix_ret(enter_syscall(SYS_GETGID, 0, 0, 0, 0, 0, 0));
}

extern "C" uid_t getuid(void)
{
    return (uid_t)__xposix_ret(enter_syscall(SYS_GETUID, 0, 0, 0, 0, 0, 0));
}

extern "C" uid_t geteuid(void)
{
    return (uid_t)__xposix_ret(enter_syscall(SYS_GETEUID, 0, 0, 0, 0, 0, 0));
}

extern "C" gid_t getegid(void)
{
    return (gid_t)__xposix_ret(enter_syscall(SYS_GETEGID, 0, 0, 0, 0, 0, 0));
}

extern "C" int getgroups(int size, gid_t list[])
{
    if (size < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)__xposix_ret(enter_syscall(SYS_GETGROUPS, size, (uint64_t)list, 0, 0, 0, 0));
}

extern "C" int dup(int fd)
{
    return (int)__xposix_ret(enter_syscall(SYS_DUP, fd, 0, 0, 0, 0, 0));
}

extern "C" int dup2(int fd, int newfd)
{
    return (int)__xposix_ret(enter_syscall(SYS_DUP2, fd, newfd, 0, 0, 0, 0));
}

extern "C" int pipe(int pipefd[2])
{
    return (int)__xposix_ret(enter_syscall(SYS_PIPE, (uint64_t)pipefd, 0, 0, 0, 0, 0));
}

extern "C" int pipe2(int pipefd[2], int flags)
{
    return (int)__xposix_ret(enter_syscall(SYS_PIPE2, (uint64_t)pipefd, flags, 0, 0, 0, 0));
}

extern "C" int access(const char *path, int mode)
{
    uint64_t required = XFA_STAT;
    if (mode & (R_OK | X_OK)) required |= XFA_READ;
    if (mode & W_OK) required |= XFA_WRITE;
    if (xj380_auto_file_access(path, required) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_ACCESS, (uint64_t)path, 0, 0, 0, 0, 0));
}

extern "C" int unlink(const char *path)
{
    if (xj380_auto_file_access(path, XFA_REMOVE) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_UNLINK, (uint64_t)path, 0, 0, 0, 0, 0));
}

extern "C" int rmdir(const char *path)
{
    if (xj380_auto_file_access(path, XFA_REMOVE) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_RMDIR, (uint64_t)path, 0, 0, 0, 0, 0));
}

extern "C" int rename(const char *oldpath, const char *newpath)
{
    if (xj380_auto_file_access(oldpath, XFA_REMOVE) < 0 ||
        xj380_auto_file_access(newpath, XFA_WRITE | XFA_CREATE) < 0) return -1;
    int64_t result = (int64_t)enter_syscall(SYS_RENAME, (uint64_t)oldpath, (uint64_t)newpath, 0, 0, 0, 0);
    // Crossing a category also exports the source data. Ask for source read
    // only when the kernel's transfer guard requires it; same-category moves
    // with SW must continue working without SR.
    if (result == -EACCES && xapi_QueryFileAccess(oldpath, XFA_READ | XFA_EXPORT) > 0 &&
        xj380_auto_file_access(oldpath, XFA_READ | XFA_EXPORT) == 0)
        result = (int64_t)enter_syscall(SYS_RENAME, (uint64_t)oldpath, (uint64_t)newpath, 0, 0, 0, 0);
    return (int)__xposix_ret((uint64_t)result);
}

extern "C" int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
    return (int)__xposix_ret(enter_syscall(SYS_RENAMEAT, olddirfd, (uint64_t)oldpath, newdirfd, (uint64_t)newpath, 0, 0));
}

extern "C" int link(const char *oldpath, const char *newpath)
{
    if (xj380_auto_file_access(oldpath, XFA_READ) < 0 ||
        xj380_auto_file_access(newpath, XFA_WRITE | XFA_CREATE) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_LINK, (uint64_t)oldpath, (uint64_t)newpath, 0, 0, 0, 0));
}

extern "C" int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    return (int)__xposix_ret(enter_syscall(SYS_LINKAT, olddirfd, (uint64_t)oldpath, newdirfd, (uint64_t)newpath, flags, 0));
}

extern "C" int symlink(const char *target, const char *linkpath)
{
    if (xj380_auto_file_access(linkpath, XFA_WRITE | XFA_CREATE | XFA_NOFOLLOW) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_SYMLINK, (uint64_t)target, (uint64_t)linkpath, 0, 0, 0, 0));
}

extern "C" int symlinkat(const char *target, int newdirfd, const char *linkpath)
{
    return (int)__xposix_ret(enter_syscall(SYS_SYMLINKAT, (uint64_t)target, newdirfd, (uint64_t)linkpath, 0, 0, 0));
}

extern "C" ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    if (xj380_auto_file_access(path, XFA_STAT | XFA_NOFOLLOW) < 0) return -1;
    return (ssize_t)__xposix_ret(enter_syscall(SYS_READLINK, (uint64_t)path, (uint64_t)buf, bufsiz, 0, 0, 0));
}

extern "C" off_t lseek(int fd, off_t offset, int whence)
{
    return (off_t)__xposix_ret(enter_syscall(SYS_LSEEK, fd, (uint64_t)offset, whence, 0, 0, 0));
}

extern "C" int fsync(int fd)
{
    return (int)__xposix_ret(enter_syscall(SYS_FSYNC, fd, 0, 0, 0, 0, 0));
}

extern "C" int fdatasync(int fd)
{
    return (int)__xposix_ret(enter_syscall(SYS_FDATASYNC, fd, 0, 0, 0, 0, 0));
}

extern "C" unsigned int sleep(unsigned int seconds)
{
    struct timespec ts = {(time_t)seconds, 0};
    nanosleep(&ts, NULL);
    return 0;
}

extern "C" int usleep(unsigned long usec)
{
    struct timespec ts = {(time_t)(usec / 1000000UL), (long)((usec % 1000000UL) * 1000UL)};
    return nanosleep(&ts, NULL);
}

extern "C" int isatty(int fd)
{
    // 早期终端兼容：标准输入/输出/错误视为 tty，其他 fd 先保守返回不是 tty。
    if (fd >= 0 && fd <= 2) return 1;
    errno = ENOTTY;
    return 0;
}

extern "C" char *ttyname(int fd)
{
    // toybox 只需要一个稳定名字用于提示/诊断；实际终端设备后续再接入 devfs。
    static char tty[] = "/dev/tty";
    if (!isatty(fd)) return NULL;
    return tty;
}
