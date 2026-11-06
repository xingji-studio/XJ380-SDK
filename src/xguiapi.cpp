#include "./include/xguiapi.h"
#include "./include/xpermissions.h"
#include "./include/libsys.h"

// 4-1
extern "C" void xapi_CreateWindow(HDLE* handle, XWINDOW* xwin) {
	enter_syscall(XAPI_CREATE_WINDOW, (UINT64)(handle), (UINT64)(xwin), 0, 0, 0, 0);
}

extern "C" void xapi_SetWindowTitle(HDLE handle, WSTR str) {
	enter_syscall(XAPI_SET_WINDOW_TITLE, handle, (UINT64)(str), 0, 0, 0, 0);
}

extern "C" void xapi_CloseWindow(HDLE handle) {
	enter_syscall(XAPI_CLOSE_WINDOW, handle, 0, 0, 0, 0, 0);
}

extern "C" void xapi_SetIcon(HDLE handle, WSTR path) {
    if (xj380_auto_file_access(path, XFA_READ) < 0) return;
	enter_syscall(XAPI_SET_ICON, handle, (UINT64)(path), 0, 0, 0, 0);
}

extern "C" void xapi_GetWindowSize(HDLE handle, UINT64* width, UINT64* height) {
	enter_syscall(XAPI_GET_WIN_SZIE, handle, (UINT64)(width), (UINT64)(height), 0, 0, 0);
}

// 4-2
extern "C" void xapi_DrawPoint(HDLE handle, UINT32 x, UINT32 y, UINT32 color) {
	enter_syscall(XAPI_DRAW_POINT, handle, x, y, color, 0, 0);
}

extern "C" void xapi_DrawLine(
    HDLE  	handle,
	UINT32 	x1,
	UINT32 	y1,
	UINT32 	x2,
	UINT32 	y2,
	UINT32 	color
) {
	enter_syscall(XAPI_DRAW_LINE, handle, x1, y1, x2, y2, color);
}

extern "C" void xapi_DrawRect(
	HDLE 	handle,
	UINT32 	x1,
	UINT32  y1,
	UINT32  x2,
	UINT32 	y2,
	UINT32 	color,
	bool  	fill
) {
	if (fill) {
		enter_syscall(XAPI_DRAW_RECT_FILL, handle, x1, y1, x2, y2, color);
	} else {
		enter_syscall(XAPI_DRAW_RECT, handle, x1, y1, x2, y2, color);
	}
}

// void xapi_DrawCircle(
// 	HDLE  	handle,
// 	UINT32 	x,
// 	UINT32 	y,
// 	UINT32 	radius,
// 	UINT32 	color,
// 	bool  	fill
// ) {
// 	if (fill) {
// 		enter_syscall(XAPI_DRAW_CIRCLE_FILL, handle, x, y, radius, color, 0);
// 	} else {
// 		enter_syscall(XAPI_DRAW_CIRCLE, handle, x, y, radius, color, 0);
// 	}
// }

extern "C" void xapi_DrawText(
	HDLE  	handle,
	UINT32 	x,
	UINT32 	y,
	WSTR 	str,
	UINT32	size,
	UINT32  color
) {
	enter_syscall(XAPI_DRAW_TEXT, handle, x, y, (UINT64)(str), size, color);
}

extern "C" void xapi_DrawTextl(
	HDLE  	handle,
	UINT32 	x,
	UINT32 	y,
	WSTR 	str,
	UINT32	size,
	UINT32  color,
	UINT32 *width
) {
	UINT64 tx = x;
	enter_syscall(XAPI_DRAW_TEXT_L, handle, (tx << 32) | y, (UINT64)(str), size, color, (UINT64)width);
}

extern "C" void xapi_DrawSWText(
	HDLE  	handle,
	UINT32 	x,
	UINT32 	y,
	WSTR 	str,
	UINT32  color
) {
	enter_syscall(XAPI_DRAW_TEXT_SW, handle, x, y < 2 ? y : y - 2, (UINT64)(str), 10, color);
}

extern "C" UINT64 xapi_CalcTextWidth(WSTR str, UINT32 size)
{
	return enter_syscall(XAPI_CALC_TEXT_WIDTH, (uint64_t)str, (uint64_t)size, 0, 0, 0, 0);
}

extern "C" INT32 xapi_DrawSvg(HDLE handle, UINT32 x, UINT32 y, UINT32 width, WSTR svgText, bool enableTrans)
{
	return (INT32)enter_syscall(XAPI_DRAW_SVG, handle, x, y, width, (UINT64)svgText, enableTrans ? 1 : 0);
}

extern "C" INT32 xapi_DrawFA(HDLE handle, UINT32 x, UINT32 y, UINT32 width, char *name, bool enableTrans)
{
	return (INT32)enter_syscall(XAPI_DRAW_FA, handle, x, y, width, (UINT64)name, enableTrans ? 1 : 0);
}


// 4-3
extern "C" void xapi_DrawBMP(
	HDLE  	handle,
	UINT32 	x,
	UINT32 	y,
	UINT32	width,
	UINT32	height,
	WSTR    path
) {
    if (xj380_auto_file_access(path, XFA_READ) < 0) return;
	enter_syscall(XAPI_DRAW_BMP, handle, x, y, width, height, (UINT64)(path));
}

extern "C" void xapi_DrawPNG(
	HDLE  	handle,
	UINT32  x,
	UINT32  y,
	UINT32	width,
	UINT32	height,
	WSTR  	path
) {
    if (xj380_auto_file_access(path, XFA_READ) < 0) return;
	enter_syscall(XAPI_DRAW_PNG, handle, x, y, width, height, (UINT64)(path));
}

extern "C" void xapi_DrawPicture(
	HDLE  	handle,
	UINT32  x,
	UINT32  y,
	UINT32	width,
	UINT32	height,
	WSTR  	path
) {
    if (xj380_auto_file_access(path, XFA_READ) < 0) return;
	enter_syscall(XAPI_DRAW_PICTURE, handle, x, y, width, height, (UINT64)(path));
}

extern "C" bool xapi_LoadPicture(
	XCOLORA *buffer,
	UINT32   width,
	UINT32   height,
	WSTR     path
) {
    if (xj380_auto_file_access(path, XFA_READ) < 0) return false;
	return enter_syscall(XAPI_LOAD_PICTURE, (UINT64)(buffer), width, height, (UINT64)(path), 0, 0) != 0;
}

extern "C" void xapi_GetPicSize(
	UINT32	*width,
	UINT32	*height,
	WSTR  	 path
) {
    if (width) *width = 0;
    if (height) *height = 0;
    if (xj380_auto_file_access(path, XFA_READ) < 0) return;
	enter_syscall(XAPI_GET_PIC_SIZE, (UINT64)(width), (UINT64)(height), (UINT64)(path), 0, 0, 0);
}

// 4-4
extern "C" void SetMsgPrcor(HDLE handle, MsgPrcor func) {
	enter_syscall(XAPI_SET_MSH_PROCOR, handle, (UINT64)(func), 0, 0, 0, 0);
}

// 4-5
extern "C" void xapi_ReadBuffer(
	HDLE  	 	handle,
	UINT32  	x,
	UINT32  	y,
	UINT32  	width,
	UINT32		height,
	XCOLOR*  	buffer
) {
	enter_syscall(XAPI_READBUFFER, handle, x, y, width, height, (UINT64)(buffer));
}

extern "C" void xapi_WriteBuffer(
	HDLE  	 	handle,
	UINT32  	x,
	UINT32  	y,
	UINT32  	width,
	UINT32  	height,
	XCOLOR*  	buffer
) {
	enter_syscall(XAPI_WRITEBUFFER, handle, x, y, width, height, (UINT64)(buffer));
}

extern "C" void xapi_ReadBufferA(
	HDLE  	 	handle,
	UINT32  	x,
	UINT32  	y,
	UINT32  	width,
	UINT32		height,
	XCOLOR*  	buffer
) {
	enter_syscall(XAPI_READBUFFERA, handle, x, y, width, height, (UINT64)(buffer));
}

extern "C" void xapi_WriteBufferA(
	HDLE  	 	handle,
	UINT32  	x,
	UINT32  	y,
	UINT32  	width,
	UINT32  	height,
	XCOLORA*  	buffer
) {
	enter_syscall(XAPI_WRITEBUFFERA, handle, x, y, width, height, (UINT64)(buffer));
}

extern "C" UINT64 xapi_CaptureScreen(XCOLORA *buffer, UINT64 pixel_capacity, UINT32 *width, UINT32 *height)
{
	return enter_syscall(XAPI_CAPTURE_SCREEN, (UINT64)buffer, pixel_capacity, (UINT64)width, (UINT64)height, 0, 0);
}

extern "C" void xapi_RefreshWindow(HDLE handle) {
	enter_syscall(XAPI_REFRESH_WINDOW, handle, 0, 0, 0, 0, 0);
}

extern "C" void xapi_RefreshPartWindow(
	HDLE 	handle,
	UINT32  x1,
	UINT32  y1,
	UINT32  x2,
	UINT32  y2
) {
	enter_syscall(XAPI_REFRESH_PART_WINDOW, handle, x1, y1, x2, y2, 0);
}

// 4-6
extern "C" void xapi_Button(
	HDLE  	handle,
	UINT64  CRLid,
	UINT64  x,
	UINT64  y,
	WSTR  	text
) {
	enter_syscall(XAPI_BUTTON, handle, x, y, CRLid, (UINT64)(text), 0);
}

extern "C" void xapi_ButtonEmp(
	HDLE  	handle,
	UINT64  CRLid,
	UINT64  x,
	UINT64  y,
	WSTR  	text
) {
	enter_syscall(XAPI_BUTTON_EMP, handle, x, y, CRLid, (UINT64)(text), 0);
}

extern "C" void xapi_DeleteButton(
	HDLE  	handle,
	UINT64  CRLid
) {
	enter_syscall(XAPI_DELETE_BUTTON, handle, CRLid, 0, 0, 0, 0);
}

extern "C" void xapi_PutSwitch(
	HDLE  	handle,
	UINT64  x,
	UINT64  y,
	UINT64  status,
	UINT64  CRLid
) {
	enter_syscall(XAPI_PUT_SWITCH, handle, x, y, status, CRLid, 0);
}

extern "C" void xapi_SetSwitch(
	HDLE  	handle,
	UINT64  CRLid,
	UINT64  status
) {
	enter_syscall(XAPI_SET_SWITCH, handle, CRLid, status, 0, 0, 0);
}

extern "C" void xapi_DeleteSwitch(
	HDLE  	handle,
	UINT64  CRLid
) {
	enter_syscall(XAPI_DEL_SWITCH, handle, CRLid, 0, 0, 0, 0);
}

extern "C" void xapi_PutVerticalScrollBar(
	HDLE  	handle,
	UINT64  x,
	UINT64  y,
	UINT64  length,
	UINT64  thumbLength,
	UINT64  CRLid
) {
	enter_syscall(XAPI_PUT_VERTICAL_SCROLL_BAR, handle, x, y, length, thumbLength, CRLid);
}

extern "C" void xapi_PutHorizontalScrollBar(
	HDLE  	handle,
	UINT64  x,
	UINT64  y,
	UINT64  length,
	UINT64  thumbLength,
	UINT64  CRLid
) {
	enter_syscall(XAPI_PUT_HORIZONTAL_SCROLL_BAR, handle, x, y, length, thumbLength, CRLid);
}

extern "C" void xapi_DeleteScrollBar(
	HDLE  	handle,
	UINT64  CRLid
) {
	enter_syscall(XAPI_DELETE_SCROLL_BAR, handle, CRLid, 0, 0, 0, 0);
}

extern "C" void xapi_SetScrollBarPosition(
	HDLE  	handle,
	UINT64  CRLid,
	UINT64  position
) {
	enter_syscall(XAPI_SET_SCROLL_BAR_POSITION, handle, CRLid, position, 0, 0, 0);
}

extern "C" UINT64 xapi_PutTextInputBox(
	HDLE    handle,
	UINT64  x,
	UINT64  y,
	UINT64  width,
	WSTR    text
) {
	return enter_syscall(XAPI_PUT_TEXT_INBOX, handle, x, y, width, (UINT64)(text), 0);
}

extern "C" void xapi_GetTextInputBox(
	UINT64 id,
	WSTR   text
) {
	enter_syscall(XAPI_GET_TEXT_INBOX, id, (UINT64)(text), 0, 0, 0, 0);
}

extern "C" void xapi_DeleteTextInputBox(
	UINT64 id
) {
	enter_syscall(XAPI_DEL_TEXT_INBOX, id, 0, 0, 0, 0, 0);
}

extern "C" void xapi_RegisterRightButtonMenu(
	HDLE  			handle,
	RightMenuItem  *items,
	UINT64 			count
) {
	enter_syscall(XAPI_REG_RB_MENU, handle, (UINT64)(items), count, 0, 0, 0);
}

extern "C" void xapi_DeleteRightButtonMenu(HDLE handle) {
	enter_syscall(XAPI_URG_RB_MENU, handle, 0, 0, 0, 0, 0);
}

#include "./include/xpaint.h"

extern "C" INT64 xapi_BeginPaint(HDLE window, UINT32 width, UINT32 height)
{
    return (INT64)enter_syscall(XAPI_WINDOW_PAINT, XPAINT_BEGIN, window, width, height, 0, 0);
}

extern "C" INT64 xapi_EndPaint(HDLE window, HDLE surface)
{
    return (INT64)enter_syscall(XAPI_WINDOW_PAINT, XPAINT_COMMIT, window, surface, 0, 0, 0);
}

extern "C" void xapi_CancelPaint(HDLE window, HDLE surface)
{
    enter_syscall(XAPI_WINDOW_PAINT, XPAINT_CANCEL, window, surface, 0, 0, 0);
}

extern "C" INT64 xapi_PaintControls(HDLE window, HDLE surface, XPaintControl *items, UINT32 count)
{
    XPaintControls request = {sizeof(XPaintControls), XPAINT_CONTROLS_VERSION, count, 0, (UINT64)items};
    return (INT64)enter_syscall(XAPI_WINDOW_PAINT, XPAINT_CONTROLS, window, surface, (UINT64)&request, 0, 0);
}
