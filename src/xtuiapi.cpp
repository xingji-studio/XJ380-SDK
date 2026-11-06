#include "./include/xtuiapi.h"
#include "./include/xpermissions.h"
#include "./include/unistd.h"
#include "./include/libsys.h"
#include "./include/xposix/errno.h"

// 3-1
extern "C" void xapi_Output(WSTR str) {
    enter_syscall(XAPI_OUTPUT, (UINT64)(str), 0, 0, 0, 0, 0);
}

extern "C" void xapi_Input(WSTR str) {
    enter_syscall(XAPI_INPUT, (UINT64)(str), 0, 0, 0, 0, 0);
}

extern "C" void xapi_Getline(WSTR str)
{
    enter_syscall(XAPI_GETLINE, (UINT64)str, 0, 0, 0, 0, 0);
}

extern "C" char xapi_Getch(void) {
   return (char)(enter_syscall(XAPI_GETCH, 0, 0, 0, 0, 0, 0));
}

extern "C" void xapi_EndLine(void) {
    enter_syscall(XAPI_ENDLINE, 0, 0, 0, 0, 0, 0);
}

extern "C" void xapi_PrintLine(WSTR str) {
    enter_syscall(XAPI_PRINTLINE, (UINT64)(str), 0, 0, 0, 0, 0);
}

extern "C" void xapi_OutputSerial(WSTR str) {
    enter_syscall(XAPI_OUTPUT_SERIAL, (UINT64)(str), 0, 0, 0, 0, 0);
}

#define SERIAL_WRITE_BUFFER_SIZE 1024
char   serial_write_buffer[SERIAL_WRITE_BUFFER_SIZE] = {0};
size_t serial_write_buffer_index                     = 0;

uint8_t serial_write_handler(Writer *writer, char ch)
{
    (void)writer;
    serial_write_buffer[serial_write_buffer_index] = ch;
    ++serial_write_buffer_index;
    if (serial_write_buffer_index >= SERIAL_WRITE_BUFFER_SIZE - 1) // 1 for '\0'
    {
        xapi_Output(serial_write_buffer);
        serial_write_buffer_index = 0;
    } // Flush buffer
    serial_write_buffer[serial_write_buffer_index] = '\0';
    return 0;
}

Writer SerialWriter = {
    .data    = serial_write_buffer,
    .handler = serial_write_handler,
};

extern "C" void xapi_Printf(WSTR fmt, ...)
{
	va_list args;
    va_start(args, fmt);
    vwprintf(&SerialWriter, fmt, args);
    // flush buffer
    if (serial_write_buffer_index > 0)
    {
        enter_syscall(XAPI_PRINTF, (UINT64)(serial_write_buffer), 0, 0, 0, 0, 0);
        serial_write_buffer_index = 0;
    }
}

// 3-2
extern "C" XFILE *xapi_OpenFile(WSTR path) {
    if (xj380_auto_file_access(path, XFA_READ) < 0) return NULL;
    return (XFILE *)enter_syscall(XAPI_OPEN_FILE, (UINT64)(path), 0, 0, 0, 0, 0);
}

extern "C" void xapi_CloseFile(XFILE* fsptr) {
    enter_syscall(XAPI_CLOSE_FILE, (UINT64)(fsptr), 0, 0, 0, 0, 0);
} 

extern "C" void  xapi_SearchFile(
	WSTR	  path,
	INT32	 *count,
	DirNode	 **dir
) {
    if (count) *count = 0;
    if (dir) *dir = NULL;
    if (xj380_auto_file_access(path, XFA_LIST) < 0)
    {
        if (count) *count = -errno;
        return;
    }
	enter_syscall(XAPI_SEARCH_FILE, (UINT64)(path), (UINT64)(count), (UINT64)(dir), 0, 0, 0);
}

extern "C" void xapi_SearchFile_freem(DirNode *dir, INT32 count)
{
	enter_syscall(XAPI_SEARCH_FILE_FREEM, (UINT64)(dir), (UINT64)(count), 0, 0, 0, 0);
}

extern "C" UINT64 xapi_Mkdir(WSTR path)
{
    if (xj380_auto_file_access(path, XFA_WRITE | XFA_CREATE) < 0) return (UINT64)-errno;
	return enter_syscall(XAPI_MAKEDIR, (uint64_t)path, 0, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_CreateFile(WSTR filename)
{
    if (xj380_auto_file_access(filename, XFA_WRITE | XFA_CREATE) < 0) return (UINT64)-errno;
    return enter_syscall(XAPI_CREATE_FILE, (uint64_t)filename, 0, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_DeleteFile(WSTR path)
{
    if (xj380_auto_file_access(path, XFA_REMOVE) < 0) return (UINT64)-errno;
    return enter_syscall(XAPI_DELETE_FILE, (uint64_t)path, 0, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_RenameFile(WSTR old_path, WSTR new_path)
{
    int result = rename(old_path, new_path);
    return result < 0 ? (UINT64)-errno : (UINT64)result;
}

extern "C" UINT64 xapi_ReadFile(WSTR filename, char *buffer, UINT64 size, UINT64 offset)
{
    if (xj380_auto_file_access(filename, XFA_READ) < 0) return (UINT64)-errno;
    return enter_syscall(XAPI_READ_FILE, (uint64_t)filename, (uint64_t)buffer, size, offset, 0, 0);
}

extern "C" UINT64 xapi_WriteFile(WSTR filename, char *buffer, UINT64 size, UINT64 offset)
{
    if (xj380_auto_file_access(filename, XFA_WRITE | XFA_CREATE) < 0) return (UINT64)-errno;
    return enter_syscall(XAPI_WRITE_FILE, (uint64_t)filename, (uint64_t)buffer, size, offset, 0, 0);
}

extern "C" UINT64 xapi_Rmdir(WSTR path)
{
    if (xj380_auto_file_access(path, XFA_REMOVE) < 0) return (UINT64)-errno;
	return enter_syscall(XAPI_REMOVEDIR, (uint64_t)path, 0, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_FileDialog(UINT64 mode, WSTR title, WSTR initial_path, char *out_path, UINT64 out_size)
{
    return enter_syscall(XAPI_FILE_DIALOG, mode, (uint64_t)title, (uint64_t)initial_path, (uint64_t)out_path, out_size, 0);
}

// 3-3
extern "C" UINT64 xcr_char2int(WSTR str) {
    char *p = str;
    UINT64 num = 0;
    int w = 10;
    int len = 0;
    while (*p != '\0') {
        len++;
        p++;
    }
    num += *p - '0';
    p--;
    for (int i = 1; i < len; i++, p--, w *= 10) {
        num += (*p - '0') * w;
    }
	return num;
}

char str1[255];
extern "C" WSTR xcr_int2char(UINT64 dec) {
    
	int radix = 10;
	char index[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"; // 索引表
	unsigned unum;										   // 存放要转换的整数的绝对值,转换的整数可能是负数
	int i = 0, j, k;									   // i用来指示设置字符串相应位，转换之后i其实就是字符串的长度；转换后顺序是逆序的，有正负的情况，k用来指示调整顺序的开始位置;j用来指示调整顺序时的交换。

	// 获取要转换的整数的绝对值
	if (radix == 10 && dec < 0) // 要转换成十进制数并且是负数
	{
		unum = (unsigned)-dec; // 将num的绝对值赋给unum
		str1[i++] = '-';		   // 在字符串最前面设置为'-'号，并且索引加1
	}
	else
		unum = (unsigned)dec; // 若是num为正，直接赋值给unum

	// 转换部分，注意转换后是逆序的
	do
	{
		str1[i++] = index[unum % (unsigned)radix]; // 取unum的最后一位，并设置为str对应位，指示索引加1
		unum /= radix;							  // unum去掉最后一位

	} while (unum); // 直至unum为0退出循环

	str1[i] = '\0'; // 在字符串最后添加'\0'字符，c语言字符串以'\0'结束。

	// 将顺序调整过来
	if (str1[0] == '-')
		k = 1; // 如果是负数，符号不用调整，从符号后面开始调整
	else
		k = 0; // 不是负数，全部都要调整

	char temp;						   // 临时变量，交换两个值时用到
	for (j = k; j <= (i - 1) / 2; j++) // 头尾一一对称交换，i其实就是字符串的长度，索引最大值比长度少1
	{
		temp = str1[j];				 // 头部赋值给临时变量
		str1[j] = str1[i - 1 + k - j]; // 尾部赋值给头部
		str1[i - 1 + k - j] = temp;	 // 将临时变量的值(其实就是之前的头部值)赋给尾部
	}

	return str1;
} 

char buf1[32];
extern "C" WSTR xcr_hex2char(UINT64 hex) {
	// 输出16进制
	char *p = buf1;
	char ch;
	int i, flag = 0;

	*p++ = '0';
	*p++ = 'x'; // 先存一个0x

	if (hex == 0)
		*p++ = '0'; // 如果是0，直接0x0趋势
	else
	{
		for (i = 60; i >= 0; i -= 4)
		{						   // 每次4位
			ch = (hex >> i) & 0xF; // 0~9, A~F
			// 28（冗余）
			if (flag || ch > 0)
			{			   // 跳过前导0
				flag = 1;  // 没有前导0就把flag设为1，这样后面再有0也不会忽略
				ch += '0'; // 0~9 => '0'~'9'
				if (ch > '9')
				{
					ch += 7; // 'A' - '9' = 7
				}
				*p++ = ch; // 写入
			}
		}
	}
	*p = '\0'; // 结束符

	return buf1;
}

extern "C" UINT32 toRGB(UINT8 r, UINT8 g, UINT8 b) {
	return r << 24 | g << 16 | b << 8 | 0xff;
}

extern "C" UINT32 toRGBA(UINT32 color) {
	return ((color >> 16) & 0xff) | ((color >> 8) & 0xff) | (color & 0xff) | ((color >> 24) & 0xff);
}

#define CC(color) \
	(((color >> 16) & 0xff) | ((color >> 8) & 0xff) | (color & 0xff) | ((color >> 24) & 0xff))

// 3-4
extern "C" UINT64  xapi_Fork(void) {
	return enter_syscall(XAPI_FORK, 0, 0, 0, 0, 0, 0);
}

extern "C" UINT64  xapi_Execve(WSTR filename, WSTR argv[], WSTR envp[]) {
    int result = execve(filename, argv, envp);
    return result < 0 ? (UINT64)-errno : (UINT64)result;
}

extern "C" UINT64  xapi_RunArgs(WSTR path, WSTR argv[]) {
    if (xj380_auto_file_access(path, XFA_READ) < 0) return (UINT64)-errno;
	return enter_syscall(XAPI_RUN_ARGS, (uint64_t)path, (uint64_t)argv, 0, 0, 0, 0);
}

extern "C" void	xapi_Exit(UINT64 value) {
	enter_syscall(SYS_EXIT, value, 0, 0, 0, 0, 0);
}

// 3-5
extern "C" void	xapi_GetSystemVersion(WSTR version) {
	enter_syscall(XAPI_GET_VERSION, (uint64_t)version, 0, 0, 0, 0, 0);
}

extern "C" UINT64	xapi_GetTime(void) {
	return enter_syscall(XAPI_GET_TIME, 0, 0, 0, 0, 0, 0);
}

extern "C" void	xapi_GetCurrentUser(UserInfo *user_info) {
	enter_syscall(XAPI_GET_CURRENT_USER, (uint64_t)user_info, 0, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_UserOobeRequired(void)
{
	return enter_syscall(XAPI_USER_OOBE_REQUIRED, 0, 0, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_UserList(LoginUserInfo *buffer, UINT64 max_count)
{
	return enter_syscall(XAPI_USER_LIST, (uint64_t)buffer, max_count, 0, 0, 0, 0);
}

extern "C" void	xapi_GetTimeX(TimeType *tm) {
	enter_syscall(XAPI_GET_TIME_X, (uint64_t)tm, 0, 0, 0, 0, 0);
}

extern "C" void	xapi_GetCpuModel(WSTR version) {
	enter_syscall(XAPI_GET_CPU_MODEL, (uint64_t)version, 0, 0, 0, 0, 0);
}

extern "C" UINT64	xapi_GetMemorySize(void) {
	return enter_syscall(XAPI_GET_MEMORY_SIZE, 0, 0, 0, 0, 0, 0);
}

extern "C" UINT64	xapi_GetTimeNano() {
	return enter_syscall(XAPI_GET_TIME_NANO, 0, 0, 0, 0, 0, 0);
}

// 3-6
extern "C" void	xapi_Broken(WSTR broken_info) {
	enter_syscall(XAPI_BROKEN, (uint64_t)broken_info, 0, 0, 0, 0, 0);
}

extern "C" void	xapi_SendAppMessage(WSTR title, WSTR text) {
	enter_syscall(XAPI_SEND_APP_MSG, (uint64_t)title, (uint64_t)text, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_SetNotifyPrcor(XApiNotifyPrcor func)
{
    return enter_syscall(XAPI_NOTIFY_SET_PROCOR, (uint64_t)func, 0, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_SendNotification(const XApiNotificationRequest *req)
{
    return enter_syscall(XAPI_NOTIFY_SEND, (uint64_t)req, 0, 0, 0, 0, 0);
}

extern "C" void 	xapi_Sleep(UINT64 ms)
{
	enter_syscall(XAPI_SLEEP, ms, 0, 0, 0, 0, 0);
}

extern "C" void 	xapi_Run(WSTR path)
{
    if (xj380_auto_file_access(path, XFA_READ) < 0) return;
	enter_syscall(XAPI_RUN, (uint64_t)path, 0, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_PowerAction(UINT64 action)
{
    if (action != XPOWER_REBOOT && action != XPOWER_SHUTDOWN) return (UINT64)-EINVAL;
    int result = xapiRequestPermissions(user_permissions_from_mask(XPERM_CS));
    if (result != 0) return result > 0 ? (UINT64)-EACCES : (UINT64)result;
    return enter_syscall(XAPI_POWER_ACTION, action, 0, 0, 0, 0, 0);
}

extern "C" void 	xapi_FlushTime(void)
{
	enter_syscall(XAPI_FLUSH_TIME, 0, 0, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_GetTaskList(XapiTaskInfo *buffer, UINT64 max_count)
{
	return enter_syscall(XAPI_GET_TASK_LIST, (uint64_t)buffer, max_count, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_KillProcess(UINT64 pid)
{
	return enter_syscall(XAPI_KILL_PROCESS, pid, 0, 0, 0, 0, 0);
}

extern "C" UINT64 xapi_GetSystemStats(XapiSystemStats *stats)
{
    return enter_syscall(XAPI_GET_SYSTEM_STATS, (uint64_t)stats, 0, 0, 0, 0, 0);
}

// 3-7
extern "C" void   *xapi_AllocateMemory(UINT64 size)
{
	return malloc(size);
}

extern "C" void    xapi_FreeMemory(void *ptr)
{
	free(ptr);
}

extern "C" void   *xapi_MapMemory(void *ptr, uint64_t size, uint32_t flags)
{
	return (void *)enter_syscall(XAPI_MAP_MEMORY, (uint64_t)ptr, size, flags, 0, 0, 0);
}

extern "C" int xapi_LiftPermissions(UserType ut)
{
    return (int)enter_syscall(XAPI_LIFT_PERMISSIONS, (UINT64)ut, 0, 0, 0, 0, 0);
}

extern "C" int xapi_GetCurrentPermissions(UserPermisson *up)
{
    return (int)enter_syscall(XAPI_GET_CURRENT_PERMISSIONS, (UINT64)up, 0, 0, 0, 0, 0);
}

extern "C" int xapiRequestPermissions(UserPermisson up)
{
    return (int)enter_syscall(XAPI_REQUEST_PERMISSIONS, user_permission_mask(up), 0, 0, 0, 0, 0);
}
