#include "include/xime.h"
#include "include/libsys.h"

extern "C" int64_t xapi_ImeRegister(const XImeRegistration *request)
{
    return (int64_t)enter_syscall(XAPI_IME_REGISTER, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_ImeEnum(XImeList *request)
{
    return (int64_t)enter_syscall(XAPI_IME_ENUM, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_ImeGetState(XImeState *request)
{
    return (int64_t)enter_syscall(XAPI_IME_GET_STATE, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_ImeSetActive(const XImeSelect *request)
{
    return (int64_t)enter_syscall(XAPI_IME_SET_ACTIVE, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_ImeUnregister(const XImeSelect *request)
{
    return (int64_t)enter_syscall(XAPI_IME_UNREGISTER, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_ImeAttach(XImeSession *request)
{
    return (int64_t)enter_syscall(XAPI_IME_ATTACH, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_ImeGetEvent(XImeEvent *request)
{
    return (int64_t)enter_syscall(XAPI_IME_GET_EVENT, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_ImeReplyEvent(const XImeReply *request)
{
    return (int64_t)enter_syscall(XAPI_IME_REPLY_EVENT, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_ImeUpdateUi(const XImeUi *request)
{
    return (int64_t)enter_syscall(XAPI_IME_UPDATE_UI, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_ImeSetMode(const XImeMode *request)
{
    return (int64_t)enter_syscall(XAPI_IME_SET_MODE, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" int64_t xapi_ImeSetContext(const XImeContext *request)
{
    return (int64_t)enter_syscall(XAPI_IME_SET_CONTEXT, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" XImeResult xapi_ImeAttachV2(XImeSessionV2 *request)
{
    return (XImeResult)enter_syscall(XAPI_IME_ATTACH, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" XImeResult xapi_ImeGetEventV2(XImeEventV2 *request)
{
    return (XImeResult)enter_syscall(XAPI_IME_GET_EVENT, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" XImeResult xapi_ImeReplyEventV2(const XImeReplyV2 *request)
{
    return (XImeResult)enter_syscall(XAPI_IME_REPLY_EVENT, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" XImeResult xapi_ImeUpdateUiV2(const XImeUiV2 *request)
{
    return (XImeResult)enter_syscall(XAPI_IME_UPDATE_UI, (uint64_t)request, 0, 0, 0, 0, 0);
}

extern "C" XImeResult xapi_ImeUserData(XImeUserData *request)
{
    return (XImeResult)enter_syscall(XAPI_IME_USER_DATA, (uint64_t)request, 0, 0, 0, 0, 0);
}
