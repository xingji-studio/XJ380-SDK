#include "./include/unistd.h"
#include "./include/ctype.h"
#include "./include/stdlib.h"
#include "./include/string.h"

// 1. memmove：安全内存拷贝（支持重叠）
void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0) {
        return dest;
    }

    size_t d_addr = (size_t)d;
    size_t s_addr = (size_t)s;

    // 内存重叠：从后往前拷贝
    if (d_addr > s_addr && d_addr - s_addr < n) {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    } else {
        // 无重叠：从前往后拷贝（同memcpy）
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    }
    return dest;
}

// 2. strnlen：带长度限制的字符串长度
size_t strnlen(const char *s, size_t maxlen)
{
    size_t len = 0;
    while (len < maxlen && s[len] != '\0') {
        len++;
    }
    return len;
}

// 3. strcasecmp：忽略大小写字符串比较
int strcasecmp(const char *s1, const char *s2)
{
    unsigned char c1, c2;
    while (1) {
        c1 = tolower((unsigned char)*s1++);
        c2 = tolower((unsigned char)*s2++);
        
        if (c1 != c2) {
            return c1 - c2;
        }
        // 同时到结尾，相等
        if (c1 == '\0') {
            return 0;
        }
    }
}

// 4. strncasecmp：忽略大小写字符串比较（带长度限制）
int strncasecmp(const char *s1, const char *s2, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c1 = (unsigned char)tolower((unsigned char)s1[i]);
        unsigned char c2 = (unsigned char)tolower((unsigned char)s2[i]);

        if (c1 != c2) {
            return c1 - c2;
        }
        if (c1 == '\0') {
            return 0;
        }
    }
    return 0;
}

// 5. memchr：在内存块中查找字符
void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char ch = (unsigned char)c;

    for (size_t i = 0; i < n; i++) {
        if (p[i] == ch) {
            return (void *)(p + i);
        }
    }
    return NULL;
}

int bcmp(const void *s1, const void *s2, size_t n)
{
    return memcmp(s1, s2, n);
}

void bcopy(const void *src, void *dest, size_t n)
{
    memmove(dest, src, n);
}

void bzero(void *s, size_t n)
{
    memset(s, 0, n);
}

char *index(const char *s, int c)
{
    return strchr(s, c);
}

char *rindex(const char *s, int c)
{
    return strrchr(s, c);
}

// 6. strdup：复制字符串到新分配的缓冲区
char *strdup(const char *str)
{
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str) + 1;
    char *out = (char *)malloc(len);
    if (out == NULL) {
        return NULL;
    }

    memcpy(out, str, len);
    return out;
}

// 7. strndup：最多复制 n 字节字符串到新分配的缓冲区
char *strndup(const char *str, size_t n)
{
    if (str == NULL) {
        return NULL;
    }

    size_t len = strnlen(str, n);
    char *out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }

    memcpy(out, str, len);
    out[len] = '\0';
    return out;
}

// 8. strtok_r：可重入字符串分割（线程安全）
char *strtok_r(char *str, const char *delim, char **saveptr)
{
    char *start, *end;

    // 首次调用传str，后续传NULL
    if (str == NULL) {
        str = *saveptr;
    }
    if (str == NULL) {
        return NULL;
    }

    // 跳过开头的分隔符
    while (*str != '\0' && strchr(delim, *str) != NULL) {
        str++;
    }

    // 已到字符串末尾
    if (*str == '\0') {
        *saveptr = str;
        return NULL;
    }

    start = str;
    // 找到下一个分隔符
    end = start;
    while (*end != '\0' && strchr(delim, *end) == NULL) {
        end++;
    }

    // 截断字符串
    if (*end != '\0') {
        *end = '\0';
        *saveptr = end + 1;
    } else {
        *saveptr = end;
    }

    return start;
}

// 9. strspn：返回前缀匹配分隔符的长度
size_t strspn(const char *s, const char *accept)
{
    size_t len = 0;
    while (s[len] != '\0' && strchr(accept, s[len]) != NULL) {
        len++;
    }
    return len;
}

// 10. strcspn：返回前缀不匹配分隔符的长度
size_t strcspn(const char *s, const char *reject)
{
    size_t len = 0;
    while (s[len] != '\0' && strchr(reject, s[len]) == NULL) {
        len++;
    }
    return len;
}

// 11. strpbrk：查找第一个匹配分隔符的字符
char *strpbrk(const char *s, const char *accept)
{
    while (*s != '\0') {
        if (strchr(accept, *s) != NULL) {
            return (char *)s;
        }
        s++;
    }
    return NULL;
}
