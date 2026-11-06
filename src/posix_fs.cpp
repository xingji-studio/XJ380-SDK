#include "./include/fcntl.h"
#include "./include/xpermissions.h"
#include "./include/dirent.h"
#include "./include/unistd.h"
#include "./include/sys/stat.h"
#include "./include/xposix/syscall_ret.h"
#include "./include/libsys.h"
#include "./include/krlibc.h"
#include "./include/liballoc/alloc.h"
#include "./include/xposix/stdarg.h"
#include "./include/sys/ioctl.h"

#define TIOCGPTN   0x80045430
#define TIOCSPTLCK 0x40045431

static uint64_t file_open_access(int flags)
{
    uint64_t access = (flags & O_PATH) ? XFA_STAT :
        (flags & O_ACCMODE) == O_WRONLY ? XFA_WRITE :
        (flags & O_ACCMODE) == O_RDWR ? XFA_READ | XFA_WRITE : XFA_READ;
    if (flags & O_DIRECTORY) access |= XFA_LIST;
    if (flags & O_CREAT) access |= XFA_CREATE | XFA_WRITE;
    if (flags & O_TRUNC) access |= XFA_WRITE;
    if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) access |= XFA_EXCLUSIVE;
    if (flags & O_NOFOLLOW) access |= XFA_NOFOLLOW;
    return access;
}

extern "C" int open(const char *path, int flags, ...)
{
    mode_t mode = 0644;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (xj380_auto_file_access(path, file_open_access(flags)) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_OPEN, (uint64_t)path, flags, mode, 0, 0, 0));
}

extern "C" int openat(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0644;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if ((path && path[0] == '/') || dirfd == AT_FDCWD)
        if (xj380_auto_file_access(path, file_open_access(flags)) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_OPENAT, dirfd, (uint64_t)path, flags, mode, 0, 0));
}

extern "C" int creat(const char *path, unsigned int mode)
{
    return open(path, O_CREAT | O_TRUNC | O_WRONLY, mode);
}

extern "C" int posix_openpt(int flags)
{
    return open("/dev/ptmx", flags);
}

extern "C" int grantpt(int fd)
{
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

extern "C" int unlockpt(int fd)
{
    int lock = 0;
    return ioctl(fd, TIOCSPTLCK, &lock);
}

extern "C" char *ptsname(int fd)
{
    static char path[32];
    int id = -1;
    if (ioctl(fd, TIOCGPTN, &id) < 0) return NULL;
    snprintf(path, sizeof(path), "/dev/pts/%d", id);
    return path;
}

extern "C" int stat(const char *path, struct stat *buf)
{
    if (xj380_auto_file_access(path, XFA_STAT) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_STAT, (uint64_t)path, (uint64_t)buf, 0, 0, 0, 0));
}

extern "C" int lstat(const char *path, struct stat *buf)
{
    if (xj380_auto_file_access(path, XFA_STAT | XFA_NOFOLLOW) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_LSTAT, (uint64_t)path, (uint64_t)buf, 0, 0, 0, 0));
}

extern "C" int fstat(int fd, struct stat *buf)
{
    return (int)__xposix_ret(enter_syscall(SYS_FSTAT, fd, (uint64_t)buf, 0, 0, 0, 0));
}

extern "C" int fstatat(int dirfd, const char *path, struct stat *buf, int flags)
{
    if ((path && path[0] == '/') || dirfd == AT_FDCWD)
        if (xj380_auto_file_access(path, XFA_STAT | ((flags & AT_SYMLINK_NOFOLLOW) ? XFA_NOFOLLOW : 0)) < 0)
            return -1;
    return (int)__xposix_ret(enter_syscall(SYS_NEWFSTATAT, dirfd, (uint64_t)path, (uint64_t)buf, flags, 0, 0));
}

extern "C" int mkdir(const char *path, mode_t mode)
{
    if (xj380_auto_file_access(path, XFA_WRITE | XFA_CREATE) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_MKDIR, (uint64_t)path, mode, 0, 0, 0, 0));
}

extern "C" int mkdirat(int dirfd, const char *path, mode_t mode)
{
    if ((path && path[0] == '/') || dirfd == AT_FDCWD)
        if (xj380_auto_file_access(path, XFA_WRITE | XFA_CREATE) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_MKDIRAT, dirfd, (uint64_t)path, mode, 0, 0, 0));
}

extern "C" int chmod(const char *path, mode_t mode)
{
    if (xj380_auto_file_access(path, XFA_WRITE) < 0) return -1;
    return (int)__xposix_ret(enter_syscall(SYS_CHMOD, (uint64_t)path, mode, 0, 0, 0, 0));
}

extern "C" int fchmod(int fd, mode_t mode)
{
    // fchmod 是 fd 版本权限设置，供 cp/install 等工具链接使用。
    return (int)__xposix_ret(enter_syscall(SYS_FCHMOD, fd, mode, 0, 0, 0, 0));
}

extern "C" int chown(const char *path, uid_t owner, gid_t group)
{
    // 当前系统没有真实属主模型；只验证参数形态并把 chown 视为成功的兼容空操作。
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    (void)owner;
    (void)group;
    return 0;
}

extern "C" int lchown(const char *path, uid_t owner, gid_t group)
{
    // 还没有符号链接属主差异，lchown 与 chown 使用相同兼容行为。
    return chown(path, owner, group);
}

extern "C" int fchown(int fd, uid_t owner, gid_t group)
{
    // fd 有效性先做最小检查；属主变更仍是兼容空操作。
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    (void)owner;
    (void)group;
    return 0;
}

extern "C" int truncate(const char *path, off_t length)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    int ret = ftruncate(fd, length);
    int saved = errno;
    close(fd);
    if (ret < 0) errno = saved;
    return ret;
}

extern "C" int ftruncate(int fd, off_t length)
{
    return (int)__xposix_ret(enter_syscall(SYS_FTRUNCATE, fd, (uint64_t)length, 0, 0, 0, 0));
}

extern "C" int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
    return (int)__xposix_ret(enter_syscall(SYS_UTIMENSAT, dirfd, (uint64_t)path, (uint64_t)times,
                                         (uint64_t)flags, 0, 0));
}

extern "C" int getdents(int fd, struct dirent *dents, size_t size)
{
    return (int)__xposix_ret(enter_syscall(SYS_GETDENTS, fd, (uint64_t)dents, size, 0, 0, 0));
}

extern "C" DIR *opendir(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return NULL;

    DIR *dir = (DIR *)calloc(1, sizeof(DIR));
    if (dir == NULL) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    dir->fd = fd;
    return dir;
}

extern "C" struct dirent *readdir(DIR *dirp)
{
    if (dirp == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (dirp->pos >= dirp->len) {
        int got = getdents(dirp->fd, (struct dirent *)dirp->buf, sizeof(dirp->buf));
        if (got <= 0) return NULL;
        dirp->pos = 0;
        dirp->len = got;
    }

    struct dirent *ent = (struct dirent *)(dirp->buf + dirp->pos);
    if (ent->d_reclen == 0 || dirp->pos + ent->d_reclen > dirp->len) {
        errno = EINVAL;
        return NULL;
    }
    dirp->pos += ent->d_reclen;
    return ent;
}

extern "C" int closedir(DIR *dirp)
{
    if (dirp == NULL) {
        errno = EINVAL;
        return -1;
    }
    int ret = close(dirp->fd);
    free(dirp);
    return ret;
}
