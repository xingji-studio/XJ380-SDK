#include "./include/x3api.h"
#include "./include/libsys.h"
#include "./include/xposix/syscall_ret.h"
#include "./include/fcntl.h"

extern "C" ssize_t read(int fd, void *buf, size_t len)
{
    return (ssize_t)__xposix_ret(enter_syscall(SYS_READ, fd, (uint64_t)buf, len, 0, 0, 0));
}

extern "C" ssize_t write(int fd, const void *buf, size_t len)
{
    return (ssize_t)__xposix_ret(enter_syscall(SYS_WRITE, fd, (uint64_t)buf, len, 0, 0, 0));
}

#define CANREAD(flag)  ((flag) & READ || (flag) & PLUS)
#define CANWRITE(flag) ((flag) & WRITE || (flag) & PLUS || (flag) & APPEND)

// 标准输入/输出/错误流：它们没有路径名，所有 I/O 都直接落到 0/1/2 号文件描述符。
static FILE g_stdin_file  = {0, READ, 0, NULL, 0, 0, 0, 0, NULL};
static FILE g_stdout_file = {1, WRITE, 0, NULL, 0, 0, 0, 0, NULL};
static FILE g_stderr_file = {2, WRITE, 0, NULL, 0, 0, 0, 0, NULL};

extern "C" FILE *stdin  = &g_stdin_file;
extern "C" FILE *stdout = &g_stdout_file;
extern "C" FILE *stderr = &g_stderr_file;

// 将 va_list 格式化后写入指定 fd；先走栈缓冲，太长时再临时 malloc，避免 printf 固定截断。
static int vformat_to_fd(int fd, const char *format, va_list ap)
{
    char stack_buffer[1024];
    va_list measure_ap;
    __builtin_va_copy(measure_ap, ap);
    int needed = vsnprintf(stack_buffer, sizeof(stack_buffer), format, measure_ap);
    va_end(measure_ap);

    if (needed <= 0) {
        return needed;
    }

    if ((size_t)needed < sizeof(stack_buffer)) {
        write(fd, stack_buffer, (size_t)needed);
        return needed;
    }

    char *heap_buffer = (char *)malloc((size_t)needed + 1);
    if (heap_buffer == NULL) {
        errno = ENOMEM;
        write(fd, stack_buffer, sizeof(stack_buffer) - 1);
        return needed;
    }

    va_list render_ap;
    __builtin_va_copy(render_ap, ap);
    vsnprintf(heap_buffer, (size_t)needed + 1, format, render_ap);
    va_end(render_ap);
    write(fd, heap_buffer, (size_t)needed);
    free(heap_buffer);
    return needed;
}

// 返回流是否已经遇到 EOF；兼容 C 语义，非零表示 EOF 已置位。
int feof(FILE *stream)
{ return stream != NULL && stream->eof ? 1 : 0; }

// 目前 FILE 结构还没有持久错误位；先提供接口，真实 I/O 错误通过返回值体现。
int ferror(FILE *stream)
{
    (void)stream;
    return 0;
}

int fseek(FILE *fp, int offset, int whence)
{
    // 文件流采用内存缓冲指针模拟 seek；标准流/空流不支持随机定位。
    if (fp == NULL || (fp->name == NULL && fp->fd < 0)) {
        errno = ESPIPE;
        return -1;
    }
    if (fp->fd >= 0)
    {
        off_t position = lseek(fp->fd, offset, whence);
        if (position < 0) return -1;
        fp->p = (unsigned int)position;
        fp->eof = 0;
        return 0;
    }
    if (whence == 0) { fp->p = offset; }
    else if (whence == 1) { fp->p += offset; }
    else if (whence == 2) { fp->p = fp->fileSize + offset; }
    else
    {
        return -1;
    }
    return 0;
}

long ftell(FILE *stream)
{
    // ftell 返回当前逻辑读写位置；标准流没有可报告位置，按错误处理。
    if (stream == NULL || (stream->name == NULL && stream->fd < 0)) {
        errno = ESPIPE;
        return -1;
    }
    return stream->fd >= 0 ? lseek(stream->fd, 0, SEEK_CUR) : stream->p;
}

FILE *fopen(const char *filename, const char *mode)
{
    // 解析 fopen 模式，并用现有 XAPI 文件接口建立一个轻量 FILE 包装。
    if (!filename || !mode) { errno = EINVAL; return NULL; }
    XFILE *xf = NULL;
    unsigned int flag = 0;
    FILE        *fp   = (FILE *)calloc(1, sizeof(FILE));
    if (fp == NULL) return NULL;
    fp->read_flag     = 0;
    while (*mode != '\0')
    {
        switch (*mode)
        {
        case 'a': flag |= APPEND; break;
        case 'b': break;
        case 'r': flag |= READ; break;
        case 'w': flag |= WRITE; break;
        case '+': flag |= PLUS; break;
        default: break;
        }
        mode++;
    }
    if (CANREAD(flag) && !CANWRITE(flag))
    {
        // Keep the existing buffered FILE/seek/ungetc behavior, but never use
        // XFILE for read-only streams: closing its mutable snapshot writes back.
        int fd = open(filename, O_RDONLY);
        if (fd < 0) { free(fp); return NULL; }
        off_t length = lseek(fd, 0, SEEK_END);
        if (length < 0 || (uint64_t)length > (unsigned int)-1 || lseek(fd, 0, SEEK_SET) < 0)
        {
            int error = length > (off_t)(unsigned int)-1 ? EFBIG : errno;
            close(fd);
            free(fp);
            errno = error;
            return NULL;
        }
        fp->bufferSize = length ? (unsigned int)length : 1;
        fp->buffer = (unsigned char *)malloc(fp->bufferSize);
        fp->name = (char *)malloc(strlen(filename) + 1);
        if (!fp->buffer || !fp->name)
        {
            close(fd); free(fp->buffer); free(fp->name); free(fp);
            errno = ENOMEM;
            return NULL;
        }
        size_t received = 0;
        while (received < (size_t)length)
        {
            ssize_t count = read(fd, fp->buffer + received, (size_t)length - received);
            if (count < 0)
            {
                int error = errno;
                close(fd); free(fp->buffer); free(fp->name); free(fp);
                errno = error;
                return NULL;
            }
            if (!count) break;
            received += (size_t)count;
        }
        close(fd);
        strcpy(fp->name, filename);
        fp->fileSize = (unsigned int)received;
        fp->fd = -1;
        fp->mode = flag;
        return fp;
    }
    if (!CANREAD(flag) && CANWRITE(flag))
    {
        int flags = O_WRONLY | O_CREAT | ((flag & APPEND) ? O_APPEND : O_TRUNC);
        int fd = open(filename, flags, 0644);
        if (fd < 0) { free(fp); return NULL; }
        fp->fd = fd;
        fp->mode = flag;
        return fp;
    }
    if (CANWRITE(flag) && xj380_auto_file_access(filename, XFA_READ | XFA_WRITE | XFA_CREATE) < 0)
    {
        free(fp);
        return NULL;
    }
    xf = xapi_OpenFile((char *)filename);
    if (xf == NULL)
    {
        if (flag & (WRITE | APPEND | PLUS)) {
            xapi_CreateFile((char *)filename);
            xf = xapi_OpenFile((char *)filename);
        }
    }
    if (xf == NULL)
    {
        free(fp);
        return NULL;
    }
    if (xf->length <= 0)
    {
        if (flag & READ)
        {
            free(fp);
            xapi_CloseFile(xf);
            return NULL; // 找不到
        }
        // vfs似乎没有创建文件
        // if (flag & WRITE || flag & APPEND) {
        // 	if (!mkfile(filename)) {
        // 		free(fp);
        // 		return NULL;
        // 	}
        // 	fp->read_flag = 2;
        // }
    }
    if (flag & WRITE) { fp->fileSize = 0; }
    else
    {
        fp->fileSize = xf->length;
    }
    fp->bufferSize = 0;
    if (flag & READ || flag & PLUS || flag & APPEND) { fp->bufferSize = xf->length; }
    if (flag & WRITE || flag & PLUS || flag & APPEND) { fp->bufferSize += 100; }
    if (fp->bufferSize == 0) { fp->bufferSize = 1; }
    fp->buffer = (unsigned char *)(malloc(fp->bufferSize));
    if (flag & PLUS || flag & APPEND || flag & READ)
    {
        if (fp->read_flag != 2) fp->read_flag = 1;
    }
    fp->p   = 0;
    fp->eof = 0;
    if (flag & APPEND) { fp->p = fp->fileSize; }
    fp->name = (char *)(malloc(strlen(filename) + 1));
    if (fp->name == NULL) {
        free(fp->buffer);
        free(fp);
        xapi_CloseFile(xf);
        return NULL;
    }
    strcpy(fp->name, filename);
    fp->fd = -1;
    fp->mode = flag;
    //	printf("[fopen]BufferSize=%d\n",fp->bufferSize);
    xapi_CloseFile(xf);
    return fp;
}

int fclose(FILE *fp)
{
    // 标准流不能被真正释放；文件流只释放用户态包装和缓冲。
    if (fp == NULL) { return EOF; }
    if (fp == stdin || fp == stdout || fp == stderr) { return 0; }
    int result = fp->fd >= 0 ? close(fp->fd) : 0;
    free(fp->buffer);
    free(fp->name);
    free(fp);
    return result;
}

int fgetc(FILE *stream)
{
    // 标准输入直接读一个字节；普通文件第一次读取时把 XAPI 缓冲拷入 FILE 缓冲。
    if (stream == NULL) {
        errno = EINVAL;
        return EOF;
    }
    if (stream == stdin) {
        unsigned char ch = 0;
        return read(0, &ch, 1) == 1 ? ch : EOF;
    }
    if (!CANREAD(stream->mode)) { errno = EBADF; return EOF; }
    if (!CANWRITE(stream->mode) && stream->read_flag == 0 && stream->buffer)
    {
        if (stream->p >= stream->fileSize) { stream->eof = 1; return EOF; }
        return stream->buffer[stream->p++];
    }
    XFILE *xf = xapi_OpenFile(stream->name);
    if (!xf) { return EOF; }
    if (CANREAD(stream->mode))
    {
        if (stream->p >= stream->fileSize || stream->fileSize == -1)
        {
            stream->eof = 1;
            xapi_CloseFile(xf);
            return EOF;
        }
        else
        {
            if (stream->read_flag == 1)
            {
                memcpy(stream->buffer, xf->buffer, stream->fileSize);
                stream->read_flag = 0;
            }
            int ch = stream->buffer[stream->p++];
            xapi_CloseFile(xf);
            return ch;
        }
    }
    else
    {
        xapi_CloseFile(xf);
        return EOF;
    }
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream)
{
    // C 标准要求返回完整元素数量，不是字节数量；短读时只统计已经读满的元素。
    if (size == 0 || count == 0) return 0;
    if (CANREAD(stream->mode))
    {
        unsigned char *c_ptr = (unsigned char *)buffer;
        for (size_t i = 0; i < size * count; i++)
        {
            int ch = fgetc(stream);
            if (ch == EOF) { return i / size; }
            else
            {
                c_ptr[i] = ch;
            }
        }
        return count;
    }
    else
    {
        return 0;
    }
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
    // 标准输出/错误直接写 fd；普通文件通过 xapi_WriteFile 写到当前偏移。
    if (buffer == NULL || stream == NULL || size == 0 || count == 0) return 0;

    size_t total = size * count;
    if (stream == stdout || stream == stderr || stream->fd >= 0) {
        int fd = fileno(stream);
        ssize_t written = write(fd, buffer, total);
        return written <= 0 ? 0 : (size_t)written / size;
    }

    if (!CANWRITE(stream->mode) || stream->name == NULL) {
        errno = EBADF;
        return 0;
    }

    UINT64 written = xapi_WriteFile(stream->name, (char *)buffer, total, stream->p);
    if ((int64_t)written < 0) return 0;
    stream->p += (unsigned int)written;
    if (stream->p > stream->fileSize) stream->fileSize = stream->p;
    return (size_t)written / size;
}

char *fgets(char *str, int size, FILE *stream)
{
    // 读取一行，最多保留 size-1 个字符，并始终在成功读取后补 NUL。
    if (str == NULL || size <= 0 || stream == NULL) {
        errno = EINVAL;
        return NULL;
    }

    int pos = 0;
    while (pos < size - 1) {
        int ch = fgetc(stream);
        if (ch == EOF) break;
        str[pos++] = (char)ch;
        if (ch == '\n') break;
    }
    if (pos == 0) return NULL;
    str[pos] = '\0';
    return str;
}

int fputc(int c, FILE *stream)
{
    // 单字符输出复用 fwrite，保证标准流和文件流路径一致。
    unsigned char ch = (unsigned char)c;
    return fwrite(&ch, 1, 1, stream) == 1 ? ch : EOF;
}

int fputs(const char *str, FILE *stream)
{
    // 输出 C 字符串，不自动追加换行；返回非负值表示成功。
    if (str == NULL) {
        errno = EINVAL;
        return EOF;
    }
    size_t len = strlen(str);
    return fwrite(str, 1, len, stream) == len ? 0 : EOF;
}

int fflush(FILE *stream)
{
    // 当前 stdio 没有延迟写缓冲，因此 flush 是空操作；NULL 表示刷新所有流，也直接成功。
    (void)stream;
    return 0;
}

int fileno(FILE *stream)
{
    // 标准流返回 0/1/2；路径型 FILE 没有内核 fd，返回 EBADF 让调用方退化处理。
    if (stream == stdin) return 0;
    if (stream == stdout) return 1;
    if (stream == stderr) return 2;
    if (stream != NULL && stream->fd >= 0) return stream->fd;
    errno = EBADF;
    return -1;
}

int ungetc(int c, FILE *fp)
{
    // 仅支持把一个字符推回到普通文件缓冲；标准输入暂不做独立回退缓冲。
    if (fp == NULL || !CANREAD(fp->mode) || !fp->buffer || fp->p == 0) { return EOF; }
    else
    {
        fp->p             -= 1;
        fp->buffer[fp->p]  = c;
        return c;
    }
}

int vprintf(const char *format, va_list ap)
{
    // vprintf 是 vfprintf(stdout, ...) 的薄封装。
    return vfprintf(stdout, format, ap);
}

int printf(const char *format, ...)
{
    // printf 使用 stdout；长输出会按需分配临时格式化缓冲。
    va_list ap;
    va_start(ap, format);
    int ret = vprintf(format, ap);
    va_end(ap);
    return ret;
}

int vdprintf(int fd, const char *format, va_list ap)
{
    // fd 版本绕过 FILE，toybox 的 perror/error 路径会用到它。
    return vformat_to_fd(fd, format, ap);
}

int dprintf(int fd, const char *format, ...)
{
    // dprintf 是 vdprintf 的可变参数入口。
    va_list ap;
    va_start(ap, format);
    int ret = vdprintf(fd, format, ap);
    va_end(ap);
    return ret;
}

int vfprintf(FILE *stream, const char *format, va_list ap)
{
    // 标准流直接格式化到 fd；普通文件先格式化到临时缓冲再 fwrite。
    if (stream == stdout || stream == stderr || stream == stdin) {
        int fd = fileno(stream);
        if (fd < 0) return -1;
        return vformat_to_fd(fd, format, ap);
    }

    char stack_buffer[1024];
    va_list measure_ap;
    __builtin_va_copy(measure_ap, ap);
    int needed = vsnprintf(stack_buffer, sizeof(stack_buffer), format, measure_ap);
    va_end(measure_ap);
    if (needed <= 0) return needed;

    if ((size_t)needed < sizeof(stack_buffer)) {
        fwrite(stack_buffer, 1, (size_t)needed, stream);
        return needed;
    }

    char *heap_buffer = (char *)malloc((size_t)needed + 1);
    if (heap_buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }
    va_list render_ap;
    __builtin_va_copy(render_ap, ap);
    vsnprintf(heap_buffer, (size_t)needed + 1, format, render_ap);
    va_end(render_ap);
    fwrite(heap_buffer, 1, (size_t)needed, stream);
    free(heap_buffer);
    return needed;
}

int fprintf(FILE *stream, const char *format, ...)
{
    // fprintf 是 vfprintf 的可变参数入口。
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}
