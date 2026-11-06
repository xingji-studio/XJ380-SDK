#include "include/xhighlight.h"
#include "include/libsys.h"

extern "C" XHighlightResult xapi_HighlightNotice(XHighlightStyle style, const char *text)
{
    return (XHighlightResult)enter_syscall(XAPI_HIGHLIGHT_NOTICE, style, (uint64_t)text, 0, 0, 0, 0);
}
