#include "include/xanimation.h"
#include "include/libsys.h"

extern "C" XAnimResult xapi_AnimationStart(const XAnimDesc *description, XAnimHandle *handle)
{
    return (XAnimResult)enter_syscall(XAPI_ANIMATION_START, (uint64_t)description, (uint64_t)handle, 0, 0, 0, 0);
}

extern "C" XAnimResult xapi_AnimationControl(XAnimHandle handle, XAnimU32 operation)
{
    return (XAnimResult)enter_syscall(XAPI_ANIMATION_CONTROL, handle, operation, 0, 0, 0, 0);
}

extern "C" XAnimResult xapi_AnimationQuery(XAnimHandle handle, XAnimSnapshot *snapshot)
{
    return (XAnimResult)enter_syscall(XAPI_ANIMATION_QUERY, handle, (uint64_t)snapshot, 0, 0, 0, 0);
}

extern "C" XAnimResult xapi_AnimationRelease(XAnimHandle handle)
{
    return (XAnimResult)enter_syscall(XAPI_ANIMATION_RELEASE, handle, 0, 0, 0, 0, 0);
}
