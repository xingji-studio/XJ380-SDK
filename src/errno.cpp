#include "./include/xposix/errno.h"
#include "./include/unistd.h"

extern "C" {
int errno = 0;
}

static size_t errno_strlen(const char *s)
{
    size_t len = 0;
    if (s == NULL) return 0;
    while (s[len] != '\0') len++;
    return len;
}

extern "C" int *__errno_location(void)
{
    return &errno;
}

extern "C" char *strerror(int errnum)
{
    const char *msg = "Unknown error";

    switch (errnum) {
    case EOK: msg = "Success"; break;
    case EPERM: msg = "Operation not permitted"; break;
    case ENOENT: msg = "No such file or directory"; break;
    case ESRCH: msg = "No such process"; break;
    case EINTR: msg = "Interrupted system call"; break;
    case EIO: msg = "I/O error"; break;
    case ENXIO: msg = "No such device or address"; break;
    case E2BIG: msg = "Argument list too long"; break;
    case ENOEXEC: msg = "Exec format error"; break;
    case EBADF: msg = "Bad file descriptor"; break;
    case ECHILD: msg = "No child processes"; break;
    case EAGAIN: msg = "Resource temporarily unavailable"; break;
    case ENOMEM: msg = "Out of memory"; break;
    case EACCES: msg = "Permission denied"; break;
    case EFAULT: msg = "Bad address"; break;
    case ENOTBLK: msg = "Block device required"; break;
    case EBUSY: msg = "Device or resource busy"; break;
    case EEXIST: msg = "File exists"; break;
    case EXDEV: msg = "Invalid cross-device link"; break;
    case ENODEV: msg = "No such device"; break;
    case ENOTDIR: msg = "Not a directory"; break;
    case EISDIR: msg = "Is a directory"; break;
    case EINVAL: msg = "Invalid argument"; break;
    case ENFILE: msg = "Too many open files in system"; break;
    case EMFILE: msg = "Too many open files"; break;
    case ENOTTY: msg = "Inappropriate ioctl for device"; break;
    case ETXTBSY: msg = "Text file busy"; break;
    case EFBIG: msg = "File too large"; break;
    case ENOSPC: msg = "No space left on device"; break;
    case ESPIPE: msg = "Illegal seek"; break;
    case EROFS: msg = "Read-only file system"; break;
    case EMLINK: msg = "Too many links"; break;
    case EPIPE: msg = "Broken pipe"; break;
    case EDOM: msg = "Numerical argument out of domain"; break;
    case ERANGE: msg = "Numerical result out of range"; break;
    case EDEADLK: msg = "Resource deadlock avoided"; break;
    case ENAMETOOLONG: msg = "File name too long"; break;
    case ENOLCK: msg = "No locks available"; break;
    case ENOSYS: msg = "Function not implemented"; break;
    case ENOTEMPTY: msg = "Directory not empty"; break;
    case ELOOP: msg = "Too many levels of symbolic links"; break;
    case ENOMSG: msg = "No message of desired type"; break;
    case EIDRM: msg = "Identifier removed"; break;
    case EILSEQ: msg = "Invalid or incomplete multibyte or wide character"; break;
    case ENOTSOCK: msg = "Socket operation on non-socket"; break;
    case EDESTADDRREQ: msg = "Destination address required"; break;
    case EMSGSIZE: msg = "Message too long"; break;
    case EPROTOTYPE: msg = "Protocol wrong type for socket"; break;
    case ENOPROTOOPT: msg = "Protocol not available"; break;
    case EPROTONOSUPPORT: msg = "Protocol not supported"; break;
    case EOPNOTSUPP: msg = "Operation not supported"; break;
    case EAFNOSUPPORT: msg = "Address family not supported by protocol"; break;
    case EADDRINUSE: msg = "Address already in use"; break;
    case EADDRNOTAVAIL: msg = "Cannot assign requested address"; break;
    case ENETDOWN: msg = "Network is down"; break;
    case ENETUNREACH: msg = "Network is unreachable"; break;
    case ENETRESET: msg = "Network dropped connection on reset"; break;
    case ECONNABORTED: msg = "Software caused connection abort"; break;
    case ECONNRESET: msg = "Connection reset by peer"; break;
    case ENOBUFS: msg = "No buffer space available"; break;
    case EISCONN: msg = "Transport endpoint is already connected"; break;
    case ENOTCONN: msg = "Transport endpoint is not connected"; break;
    case ESHUTDOWN: msg = "Cannot send after transport endpoint shutdown"; break;
    case ETIMEDOUT: msg = "Connection timed out"; break;
    case ECONNREFUSED: msg = "Connection refused"; break;
    case EHOSTDOWN: msg = "Host is down"; break;
    case EHOSTUNREACH: msg = "No route to host"; break;
    case EALREADY: msg = "Operation already in progress"; break;
    case EINPROGRESS: msg = "Operation now in progress"; break;
    case ESTALE: msg = "Stale file handle"; break;
    case EOVERFLOW: msg = "Value too large for defined data type"; break;
    case ENOMEDIUM: msg = "No medium found"; break;
    case EMEDIUMTYPE: msg = "Wrong medium type"; break;//eeeeee我要炸了
    default: break;
    }

    return (char *)msg;
}

extern "C" void perror(const char *prefix)
{
    int saved_errno = errno;
    if (prefix != NULL && prefix[0] != '\0') {
        write(2, prefix, errno_strlen(prefix));
        write(2, ": ", 2);
    }

    char *msg = strerror(saved_errno);
    write(2, msg, errno_strlen(msg));
    write(2, "\n", 1);
    errno = saved_errno;
}
