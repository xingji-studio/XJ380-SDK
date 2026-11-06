#include "./include/xposix.h"
#include "./include/libsys.h"
#ifdef XAPI_HEAP_BINS
#include "heap_bins.h"
#endif

extern "C" char **environ;

namespace
{

#ifndef XAPI_HEAP_BINS
static const size_t HEAP_ALIGN = 16;
static const size_t HEAP_CHUNK_SIZE = 64 * 1024;
static const size_t PAGE_SIZE = 4096;
static const uint64_t HEAP_MAGIC = 0x584A333830484541ULL;

struct heap_block
{
    size_t size;
    heap_block *prev;
    heap_block *next;
    bool free;
    uint64_t magic;
};

static heap_block *g_heap_head = NULL;
static heap_block *g_heap_tail = NULL;
static uint8_t *g_heap_base = NULL;
static uint8_t *g_heap_end = NULL;

static size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static heap_block *ptr_to_block(void *ptr)
{
    return (heap_block *)((uint8_t *)ptr - sizeof(heap_block));
}

static bool pointer_in_heap(void *ptr)
{
    uint8_t *byte_ptr = (uint8_t *)ptr;
    return g_heap_base != NULL &&
           byte_ptr >= g_heap_base + sizeof(heap_block) &&
           byte_ptr < g_heap_end;
}

static void split_block(heap_block *block, size_t size)
{
    size_t aligned_size = align_up(size, HEAP_ALIGN);
    if (block->size < aligned_size + sizeof(heap_block) + HEAP_ALIGN) {
        return;
    }

    heap_block *next = (heap_block *)((uint8_t *)(block + 1) + aligned_size);
    next->size = block->size - aligned_size - sizeof(heap_block);
    next->prev = block;
    next->next = block->next;
    next->free = true;
    next->magic = HEAP_MAGIC;

    if (next->next) {
        next->next->prev = next;
    } else {
        g_heap_tail = next;
    }

    block->size = aligned_size;
    block->next = next;
}

static void coalesce(heap_block *block)
{
    if (block->next && block->next->free && block->next->magic == HEAP_MAGIC) {
        heap_block *next = block->next;
        block->size += sizeof(heap_block) + next->size;
        block->next = next->next;
        if (block->next) {
            block->next->prev = block;
        } else {
            g_heap_tail = block;
        }
    }

    if (block->prev && block->prev->free && block->prev->magic == HEAP_MAGIC) {
        heap_block *prev = block->prev;
        prev->size += sizeof(heap_block) + block->size;
        prev->next = block->next;
        if (prev->next) {
            prev->next->prev = prev;
        } else {
            g_heap_tail = prev;
        }
    }
}

static heap_block *request_block(size_t size)
{
    size_t payload_size = align_up(size, HEAP_ALIGN);
    size_t request_size = align_up(payload_size + sizeof(heap_block), PAGE_SIZE);
    if (request_size < HEAP_CHUNK_SIZE) {
        request_size = HEAP_CHUNK_SIZE;
    }

    void *memory = sbrk((intptr_t)request_size);
    if (memory == (void *)-1) {
        return NULL;
    }

    heap_block *block = (heap_block *)memory;
    block->size = request_size - sizeof(heap_block);
    block->prev = g_heap_tail;
    block->next = NULL;
    block->free = true;
    block->magic = HEAP_MAGIC;

    if (g_heap_tail) {
        g_heap_tail->next = block;
    } else {
        g_heap_head = block;
        g_heap_base = (uint8_t *)memory;
    }
    g_heap_tail = block;
    g_heap_end = (uint8_t *)memory + request_size;

    return block;
}

#else

static void *browser_heap_grow(size_t bytes)
{
    if (bytes > ((size_t)-1 >> 1)) return (void *)-1;
    return sbrk((intptr_t)bytes);
}

static void browser_heap_wait()
{
    enter_syscall(XAPI_SLEEP, 0, 0, 0, 0, 0, 0);
}

// Constant initialization is required: TLS startup and C++ constructors can
// allocate before main. Every browser library shares this allocator instance.
static xapi_heap::Heap g_browser_heap(browser_heap_grow, browser_heap_wait);

#endif

static bool g_environ_owned = false;

// 计算环境变量名长度；遇到 '=' 或字符串结尾停止，便于匹配 NAME=value。
static size_t env_name_len(const char *name)
{
    size_t len = 0;
    while (name[len] != '\0' && name[len] != '=') len++;
    return len;
}

// 检查环境变量名是否合法；POSIX 不允许空名，也不允许名字里带 '='。
static bool valid_env_name(const char *name)
{
    return name != NULL && name[0] != '\0' && strchr(name, '=') == NULL;
}

// 查找环境变量在 environ 中的位置；未找到时返回 -1。
static int find_env_index(const char *name)
{
    if (environ == NULL || name == NULL) return -1;
    size_t name_len = env_name_len(name);
    for (int i = 0; environ[i] != NULL; i++) {
        if (strncmp(environ[i], name, name_len) == 0 && environ[i][name_len] == '=') {
            return i;
        }
    }
    return -1;
}

// 第一次 setenv/unsetenv 前，把启动器传入的环境表复制到 malloc 管理的内存中。
static bool ensure_environ_owned(void)
{
    if (g_environ_owned) return true;

    size_t count = 0;
    if (environ != NULL) {
        while (environ[count] != NULL) count++;
    }

    char **copy = (char **)calloc(count + 1, sizeof(char *));
    if (copy == NULL) return false;

    for (size_t i = 0; i < count; i++) {
        copy[i] = strdup(environ[i]);
        if (copy[i] == NULL) {
            for (size_t j = 0; j < i; j++) free(copy[j]);
            free(copy);
            return false;
        }
    }

    environ = copy;
    g_environ_owned = true;
    return true;
}

// 释放当前由 setenv 创建的一项环境变量字符串。
static void free_env_slot(int index)
{
    if (g_environ_owned && environ != NULL && index >= 0) {
        free(environ[index]);
    }
}

// 将字符转换为指定进制中的数值；非法字符返回 -1。
static int digit_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return -1;
}

// 无符号整数解析核心：同时处理空白、正负号、0/0x 前缀和溢出。
static unsigned long long parse_unsigned(const char *nptr, char **endptr, int base, bool *negative)
{
    const char *s = nptr;
    while (isspace((unsigned char)*s)) s++;

    bool neg = false;
    if (*s == '+' || *s == '-') {
        neg = (*s == '-');
        s++;
    }
    if (negative != NULL) *negative = neg;

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        int first_digit = digit_value((unsigned char)s[2]);
        if (first_digit >= 0 && first_digit < 16) {
            s += 2;
            base = 16;
        }
    }
    if (base == 0) base = (s[0] == '0') ? 8 : 10;
    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (endptr != NULL) *endptr = (char *)nptr;
        return 0;
    }

    const unsigned long long max_value = ~0ULL;
    unsigned long long value = 0;
    bool any = false;
    bool overflow = false;

    for (;; s++) {
        int digit = digit_value((unsigned char)*s);
        if (digit < 0 || digit >= base) break;
        any = true;
        if (value > (max_value - (unsigned long long)digit) / (unsigned long long)base) {
            overflow = true;
        } else {
            value = value * (unsigned long long)base + (unsigned long long)digit;
        }
    }

    if (!any) {
        if (endptr != NULL) *endptr = (char *)nptr;
        return 0;
    }

    if (overflow) {
        errno = ERANGE;
        value = max_value;
    }

    if (endptr != NULL) *endptr = (char *)s;
    if (neg && negative == NULL) return (unsigned long long)(0ULL - value);
    return value;
}

// 交换两个元素的原始字节，供 qsort 的通用元素交换使用。
static void swap_bytes(unsigned char *a, unsigned char *b, size_t size)
{
    while (size-- > 0) {
        unsigned char tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}

} // namespace

extern "C" char **environ = NULL;

#ifdef XAPI_HEAP_BINS
extern "C" int xapi_browser_heap_stats(xapi_heap::Statistics *out)
{
    if (!out) return -EINVAL;
    *out = g_browser_heap.statistics();
    return 0;
}
#endif

extern "C" void exit(int status)
{
    enter_syscall(SYS_EXIT_GROUP, status, 0, 0, 0, 0, 0);
}

extern "C" void _exit(int status)
{
    enter_syscall(SYS_EXIT, status, 0, 0, 0, 0, 0);
    for (;;) {}
}

extern "C" void abort(void)
{
    _exit(134);
}

extern "C" int brk(void *addr)
{
    uint64_t requested = (uint64_t)addr;
    uint64_t actual = enter_syscall(SYS_BRK, requested, 0, 0, 0, 0, 0);
    if (actual < requested) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

extern "C" void *sbrk(intptr_t increment)
{
    uint64_t current = enter_syscall(SYS_BRK, 0, 0, 0, 0, 0, 0);
    if (increment == 0) {
        return (void *)current;
    }

    uint64_t next = current + increment;
    if ((increment > 0 && next < current) || (increment < 0 && next > current)) {
        errno = ENOMEM;
        return (void *)-1;
    }

    uint64_t actual = enter_syscall(SYS_BRK, next, 0, 0, 0, 0, 0);
    if ((increment > 0 && actual < next) || (increment < 0 && actual > next)) {
        errno = ENOMEM;
        return (void *)-1;
    }

    return (void *)current;
}

extern "C" void *malloc(size_t size)
{
#ifdef XAPI_HEAP_BINS
    void *result = g_browser_heap.allocate(size);
    if (!result) errno = ENOMEM;
    return result;
#else
    if (size == 0) {
        size = 1;
    }
    size = align_up(size, HEAP_ALIGN);

    for (heap_block *block = g_heap_head; block; block = block->next) {
        if (block->free && block->size >= size) {
            split_block(block, size);
            block->free = false;
            return block + 1;
        }
    }

    heap_block *block = request_block(size);
    if (!block) {
        errno = ENOMEM;
        return NULL;
    }

    split_block(block, size);
    block->free = false;
    return block + 1;
#endif
}

extern "C" void *calloc(size_t nmemb, size_t size)
{
    if (size != 0 && nmemb > ((size_t)-1) / size) {
        errno = ENOMEM;
        return NULL;
    }

    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

extern "C" void free(void *ptr)
{
#ifdef XAPI_HEAP_BINS
    g_browser_heap.deallocate(ptr);
#else
    if (!ptr) {
        return;
    }

    if (!pointer_in_heap(ptr)) {
        return;
    }

    heap_block *block = ptr_to_block(ptr);
    if (block->magic != HEAP_MAGIC) {
        return;
    }

    block->free = true;
    coalesce(block);
#endif
}

extern "C" void *realloc(void *ptr, size_t size)
{
#ifdef XAPI_HEAP_BINS
    if (ptr && !g_browser_heap.owns(ptr))
    {
        errno = EINVAL;
        return NULL;
    }
    void *result = g_browser_heap.reallocate(ptr, size);
    if (!result && (size || !ptr)) errno = ENOMEM;
    return result;
#else
    if (!ptr) {
        return malloc(size);
    }

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    if (!pointer_in_heap(ptr)) {
        errno = EINVAL;
        return NULL;
    }

    heap_block *block = ptr_to_block(ptr);
    if (block->magic != HEAP_MAGIC) {
        errno = EINVAL;
        return NULL;
    }

    size = align_up(size, HEAP_ALIGN);
    if (block->size >= size) {
        split_block(block, size);
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
#endif
}

extern "C" int abs(int value)
{
    return value < 0 ? -value : value;
}

extern "C" long atol(const char *nptr)
{
    // atol 直接复用已有 strtol，保持空白/符号/进制处理一致。
    return (long)strtol(nptr, NULL, 10);
}

extern "C" long long atoll(const char *nptr)
{
    // atoll 使用 strtoll 的十进制路径，便于 toybox 解析数字参数。
    return strtoll(nptr, NULL, 10);
}

extern "C" uint64_t strtoul(const char *nptr, char **endptr, int base)
{
    // strtoul 返回 unsigned long 宽度；当前 ABI 下 unsigned long 与 uint64_t 同宽。
    return (uint64_t)strtoull(nptr, endptr, base);
}

extern "C" unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
    // strtoull 是无符号转换的公开入口，负号按 C 语义转换为模 2^N 的结果。
    return parse_unsigned(nptr, endptr, base, NULL);
}

extern "C" long long strtoll(const char *nptr, char **endptr, int base)
{
    // strtoll 在无符号解析基础上做有符号范围钳制，并设置 ERANGE。
    bool neg = false;
    unsigned long long value = parse_unsigned(nptr, endptr, base, &neg);
    const unsigned long long max_pos = 0x7fffffffffffffffULL;
    const unsigned long long max_neg = max_pos + 1ULL;

    if (neg) {
        if (value > max_neg) {
            errno = ERANGE;
            return (long long)0x8000000000000000ULL;
        }
        return -(long long)value;
    }

    if (value > max_pos) {
        errno = ERANGE;
        return (long long)max_pos;
    }
    return (long long)value;
}

extern "C" char *getenv(const char *name)
{
    // getenv 返回 environ 中 value 部分的指针；调用者不能 free 或修改它。
    int index = find_env_index(name);
    if (index < 0) return NULL;
    char *equal = strchr(environ[index], '=');
    return equal == NULL ? NULL : equal + 1;
}

extern "C" int setenv(const char *name, const char *value, int overwrite)
{
    // setenv 会维护一份用户态可写环境表，避免改写内核传入的 argv/envp 原始区域。
    if (!valid_env_name(name) || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!ensure_environ_owned()) {
        errno = ENOMEM;
        return -1;
    }

    int index = find_env_index(name);
    if (index >= 0 && !overwrite) return 0;

    size_t name_len = strlen(name);
    size_t value_len = strlen(value);
    char *entry = (char *)malloc(name_len + value_len + 2);
    if (entry == NULL) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(entry, name, name_len);
    entry[name_len] = '=';
    memcpy(entry + name_len + 1, value, value_len + 1);

    if (index >= 0) {
        free_env_slot(index);
        environ[index] = entry;
        return 0;
    }

    size_t count = 0;
    while (environ[count] != NULL) count++;

    char **grown = (char **)realloc(environ, (count + 2) * sizeof(char *));
    if (grown == NULL) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    environ = grown;
    environ[count] = entry;
    environ[count + 1] = NULL;
    return 0;
}

extern "C" int unsetenv(const char *name)
{
    // unsetenv 删除匹配项并保持 environ 以 NULL 结尾；不存在时按 POSIX 返回成功。
    if (!valid_env_name(name)) {
        errno = EINVAL;
        return -1;
    }
    if (!ensure_environ_owned()) {
        errno = ENOMEM;
        return -1;
    }

    int index = find_env_index(name);
    if (index < 0) return 0;

    free_env_slot(index);
    for (int i = index; environ[i] != NULL; i++) {
        environ[i] = environ[i + 1];
    }
    return 0;
}

extern "C" void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    // 简单稳定的插入排序：复杂度不是最优，但实现小且适合 toybox 早期 bring-up。
    if (base == NULL || compar == NULL || size == 0 || nmemb < 2) return;

    unsigned char *bytes = (unsigned char *)base;
    for (size_t i = 1; i < nmemb; i++) {
        size_t j = i;
        while (j > 0 && compar(bytes + j * size, bytes + (j - 1) * size) < 0) {
            swap_bytes(bytes + j * size, bytes + (j - 1) * size, size);
            j--;
        }
    }
}

extern "C" void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
                         int (*compar)(const void *, const void *))
{
    // 标准二分查找，要求调用方传入已按 compar 排序的数组。
    if (key == NULL || base == NULL || compar == NULL || size == 0) return NULL;

    const unsigned char *bytes = (const unsigned char *)base;
    size_t left = 0;
    size_t right = nmemb;
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const void *elem = bytes + mid * size;
        int cmp = compar(key, elem);
        if (cmp == 0) return (void *)elem;
        if (cmp < 0) right = mid;
        else left = mid + 1;
    }
    return NULL;
}
