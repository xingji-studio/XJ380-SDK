#include "include/xclipboard.h"
#include "include/libsys.h"

extern "C" XClipResult xapi_ClipboardSetData(const XClipOffer *offer)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD, XCLIP_PUBLISH, (uint64_t)offer, 0, 0, 0, 0);
}

extern "C" XClipResult xapi_ClipboardSetText(const char *text, XClipU64 bytes)
{
    XClipOffer offer = {};
    offer.size       = sizeof(offer);
    offer.version    = XCLIP_ABI_VERSION;
    offer.count      = 1;
    offer.entries[0] = {XCLIP_TEXT, 0, (XClipU64)text, bytes};
    return xapi_ClipboardSetData(&offer);
}

extern "C" XClipResult xapi_ClipboardQuery(XClipInfo *info)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD, XCLIP_QUERY, 0, (uint64_t)info, 0, 0, 0);
}

extern "C" XClipResult xapi_ClipboardAttach(XClipU64 window, XClipU32 profile, XClipU32 formats)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD_GUI, XCLIP_GUI_ATTACH, window, profile, formats, 0, 0);
}

extern "C" XClipResult xapi_RequestPaste(XClipU64 window, XClipU64 control)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD_GUI, XCLIP_GUI_PASTE, window, control, 0, 0, 0);
}

extern "C" XClipResult xapi_GetPasteInfo(XClipU64 paste, XClipInfo *info)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD, XCLIP_PASTE_INFO, paste, (uint64_t)info, 0, 0, 0);
}

extern "C" XClipResult xapi_ReadPaste(XClipU64 paste, XClipU32 format, void *buffer, XClipU64 capacity)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD, XCLIP_PASTE_READ, paste, format, (uint64_t)buffer, capacity, 0);
}

extern "C" XClipResult xapi_FinishPaste(XClipU64 paste, XClipU32 result)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD, XCLIP_PASTE_FINISH, paste, result, 0, 0, 0);
}

extern "C" XClipResult xapi_ClipboardClaimMove(XClipU64 paste)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD, XCLIP_CLAIM_MOVE, paste, 0, 0, 0, 0);
}

extern "C" XClipResult xapi_TextInputGetSelection(XClipU64 window, XClipU64 control, XTextSelection *selection)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD_GUI, XCLIP_GUI_SELECTION, window, control, (uint64_t)selection, 0,
                                      0);
}

extern "C" XClipResult xapi_TextInputSetSelection(XClipU64 window, XClipU64 control, const XTextSelection *selection)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD_GUI, XCLIP_GUI_SELECTION, window, control, (uint64_t)selection, 1,
                                      0);
}

extern "C" XClipResult xapi_TextInputSetFlags(XClipU64 window, XClipU64 control, XClipU32 flags)
{
    return (XClipResult)enter_syscall(XAPI_CLIPBOARD_GUI, XCLIP_GUI_TEXT_FLAGS, window, control, flags, 0, 0);
}
