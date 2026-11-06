#include "./include/xguiapi.h"
#include "./include/libsys.h"

extern "C" INT64 xapi_GetTextMetrics(const XTextStyle *style, const char *text, XTextMetrics *metrics)
{
    if (style == NULL || metrics == NULL) return -22;
    XTextRequest request = {};
    request.size = sizeof(request);
    request.version = XTEXT_VERSION;
    request.operation = text ? XTEXT_MEASURE : XTEXT_GET_FONT;
    request.text = (UINT64)text;
    request.result = (UINT64)metrics;
    request.style = *style;
    return (INT64)enter_syscall(XAPI_TEXT_LAYOUT, (UINT64)&request, 0, 0, 0, 0, 0);
}

extern "C" INT64 xapi_GetTextPosition(const XTextStyle *style, const char *text, UINT32 byte_offset, INT32 *position_x)
{
    if (style == NULL || text == NULL || position_x == NULL) return -22;
    XTextMetrics metrics = {};
    XTextRequest request = {};
    request.size = sizeof(request);
    request.version = XTEXT_VERSION;
    request.operation = XTEXT_CARET;
    request.text = (UINT64)text;
    request.result = (UINT64)&metrics;
    request.style = *style;
    request.byte_offset = byte_offset;
    INT64 result = (INT64)enter_syscall(XAPI_TEXT_LAYOUT, (UINT64)&request, 0, 0, 0, 0, 0);
    if (result == 0) *position_x = metrics.advance_x;
    return result;
}

extern "C" INT64 xapi_DrawTextEx(HDLE handle, INT32 x, INT32 y, const char *text, const XTextStyle *style,
                                 UINT32 color, const XTextRect *clip)
{
    if (style == NULL || text == NULL) return -22;
    XTextRequest request = {};
    request.size = sizeof(request);
    request.version = XTEXT_VERSION;
    request.operation = XTEXT_DRAW;
    request.handle = handle;
    request.text = (UINT64)text;
    request.style = *style;
    request.x = x;
    request.y = y;
    // XAPI drawing colors are RGBA; the text request stores ARGB.
    request.color = (color >> 8) | (color << 24);
    if (style->anchor == XTEXT_ANCHOR_REFERENCE) request.flags |= XTEXT_INK_LEFT;
    if (clip != NULL)
    {
        request.flags |= XTEXT_CLIP;
        request.clip_x = clip->x;
        request.clip_y = clip->y;
        request.clip_width = clip->width;
        request.clip_height = clip->height;
    }
    return (INT64)enter_syscall(XAPI_TEXT_LAYOUT, (UINT64)&request, 0, 0, 0, 0, 0);
}

extern "C" INT64 xapi_DrawTextBaseline(HDLE handle, INT32 x, INT32 y, const char *text, const XTextStyle *style,
                                       UINT32 color, const XTextRect *clip)
{
    if (style == NULL) return -22;
    XTextStyle baseline = *style;
    baseline.anchor = XTEXT_ANCHOR_BASELINE;
    return xapi_DrawTextEx(handle, x, y, text, &baseline, color, clip);
}

extern "C" void xapi_DrawTextTop(HDLE handle, INT32 x, INT32 y, WSTR text, UINT32 size, UINT32 color)
{
    const XTextStyle style = {XTEXT_FONT_UI, size, XTEXT_SIZE_NATIVE, XTEXT_ANCHOR_REFERENCE,
                              XTEXT_REFERENCE_AUTO, 0};
    xapi_DrawTextEx(handle, x, y, text, &style, color, NULL);
}

extern "C" void xapi_DrawMonoTextTop(HDLE handle, INT32 x, INT32 y, WSTR text, UINT32 color)
{
    const XTextStyle style = {XTEXT_FONT_MONO, 10, XTEXT_SIZE_NATIVE, XTEXT_ANCHOR_REFERENCE,
                              XTEXT_REFERENCE_AUTO, 0};
    xapi_DrawTextEx(handle, x, y, text, &style, color, NULL);
}

extern "C" void xapi_DrawTextInRect(HDLE handle, INT32 x, INT32 y, INT32 width, INT32 height, WSTR text,
                                    UINT32 size, UINT32 color)
{
    if (width <= 0 || height <= 0) return;
    const XTextStyle style = {XTEXT_FONT_UI, size, XTEXT_SIZE_NATIVE, XTEXT_ANCHOR_REFERENCE,
                              XTEXT_REFERENCE_AUTO, 0};
    XTextMetrics metrics = {};
    if (xapi_GetTextMetrics(&style, text, &metrics) < 0) return;
    const INT64 ink_width = (INT64)metrics.ink_right - metrics.ink_left;
    const INT64 reference_height = (INT64)metrics.reference_bottom - metrics.reference_top;
    const INT64 draw_x = (INT64)x + ((INT64)width - ink_width) / 2;
    const INT64 draw_y = (INT64)y + ((INT64)height - reference_height) / 2;
    if (draw_x < (-2147483647LL - 1) || draw_x > 2147483647LL ||
        draw_y < (-2147483647LL - 1) || draw_y > 2147483647LL)
        return;
    const XTextRect clip = {x, y, width, height};
    xapi_DrawTextEx(handle, (INT32)draw_x, (INT32)draw_y, text, &style, color, &clip);
}
