#include "./include/xpermissions.h"
#include "./include/xtuiapi.h"
#include "./include/libsys.h"
#include "./include/xposix/errno.h"

static uint64_t automatic_attempts;
static bool automatic_permissions = true;

extern "C" INT64 xapi_QueryFileAccess(const char *path, UINT64 access)
{
    return (INT64)enter_syscall(XAPI_QUERY_FILE_ACCESS, (uint64_t)path, access, 0, 0, 0, 0);
}

extern "C" INT64 xapi_RequestFileAccess(const char *path, UINT64 access)
{
    return (INT64)enter_syscall(XAPI_REQUEST_FILE_ACCESS, (uint64_t)path, access, XPERM_ALL, 0, 0, 0);
}

static int access_result(INT64 result)
{
    if (result == 0) return 0;
    errno = result > 0 ? EACCES : (int)-result;
    return -1;
}

extern "C" int xj380_ensure_file_access(const char *path, uint64_t access)
{
    return access_result(xapi_RequestFileAccess(path, access));
}

extern "C" int xj380_auto_file_access(const char *path, uint64_t access)
{
    INT64 needed = xapi_QueryFileAccess(path, access);
    // Special scopes are not ordinary seven-bit failures. Their persistent
    // decision is queried/applied by the kernel; remembered refusal never
    // displays again. Ordinary grants are not offered on this retry.
    if (needed == -EACCES && __atomic_load_n(&automatic_permissions, __ATOMIC_ACQUIRE))
        return access_result((INT64)enter_syscall(XAPI_REQUEST_FILE_ACCESS, (uint64_t)path, access, 0, 0, 0, 0));
    if (needed <= 0) return access_result(needed);
    if (needed & ~XPERM_ALL) return access_result(-EIO);
    if (!__atomic_load_n(&automatic_permissions, __ATOMIC_ACQUIRE)) return access_result(needed);
    // PID-tagged state resets naturally after fork. Claim bits before sleeping
    // in the kernel; another thread or a repaint must never duplicate a prompt.
    uint64_t pid = enter_syscall(SYS_GETPID, 0, 0, 0, 0, 0, 0);
    uint64_t tag = pid << 8;
    uint64_t previous = __atomic_load_n(&automatic_attempts, __ATOMIC_ACQUIRE);
    uint8_t offered;
    for (;;)
    {
        uint8_t attempted = (previous & ~255ULL) == tag ? (uint8_t)previous : 0;
        offered = (uint8_t)needed & ~attempted;
        if (!offered) return access_result(needed);
        uint64_t next = tag | attempted | offered;
        if (__atomic_compare_exchange_n(&automatic_attempts, &previous, next, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) break;
    }
    return access_result((INT64)enter_syscall(XAPI_REQUEST_FILE_ACCESS, (uint64_t)path, access, offered, 0, 0, 0));
}

extern "C" void xj380_reset_permission_prompts(void)
{
    __atomic_store_n(&automatic_attempts, 0ULL, __ATOMIC_RELEASE);
}

extern "C" void xj380_set_automatic_permissions(bool enabled)
{
    __atomic_store_n(&automatic_permissions, enabled, __ATOMIC_RELEASE);
}

extern "C" INT64 xapi_RequestSpecialPermission(uint64_t spid)
{
    return (INT64)enter_syscall(XAPI_REQUEST_SPECIAL_PERMISSION, spid, 0, 0, 0, 0, 0);
}
