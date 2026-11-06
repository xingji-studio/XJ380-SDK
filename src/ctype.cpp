#include "./include/ctype.h"
#include "./include/stdint.h"

enum
{
    X_CTYPE_UPPER  = 0x0100,
    X_CTYPE_LOWER  = 0x0200,
    X_CTYPE_ALPHA  = 0x0400,
    X_CTYPE_DIGIT  = 0x0800,
    X_CTYPE_XDIGIT = 0x1000,
    X_CTYPE_SPACE  = 0x2000,
    X_CTYPE_PRINT  = 0x4000,
    X_CTYPE_GRAPH  = 0x8000,
    X_CTYPE_BLANK  = 0x0001,
    X_CTYPE_CNTRL  = 0x0002,
    X_CTYPE_PUNCT  = 0x0004,
    X_CTYPE_ALNUM  = 0x0008,
};

static unsigned short int g_ctype_b_table[384];
static int32_t            g_ctype_tolower_table[384];
static int32_t            g_ctype_toupper_table[384];
static const unsigned short int *g_ctype_b_ptr = g_ctype_b_table + 128;
static const int32_t *g_ctype_tolower_ptr = g_ctype_tolower_table + 128;
static const int32_t *g_ctype_toupper_ptr = g_ctype_toupper_table + 128;
static bool g_ctype_tables_ready = false;

static bool x_isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

static bool x_islower(int c)
{
    return c >= 'a' && c <= 'z';
}

static bool x_isalpha(int c)
{
    return x_isupper(c) || x_islower(c);
}

static bool x_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

static bool x_isxdigit(int c)
{
    return x_isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static bool x_isspace(int c)
{
    return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v';
}

static bool x_isblank(int c)
{
    return c == ' ' || c == '\t';
}

static bool x_iscntrl(int c)
{
    return (c >= 0 && c < 0x20) || c == 0x7f;
}

static bool x_isprint(int c)
{
    return c >= ' ' && c <= '~';
}

static bool x_isgraph(int c)
{
    return c > ' ' && c <= '~';
}

static int x_tolower(int c)
{
    return x_isupper(c) ? c - 'A' + 'a' : c;
}

static int x_toupper(int c)
{
    return x_islower(c) ? c - 'a' + 'A' : c;
}

static void ensure_ctype_tables()
{
    if (g_ctype_tables_ready) return;

    for (int i = 0; i < 384; ++i)
    {
        int c = i - 128;
        g_ctype_b_table[i] = 0;
        g_ctype_tolower_table[i] = c;
        g_ctype_toupper_table[i] = c;

        if (c < 0 || c > 255)
        {
            if (c == -1)
            {
                g_ctype_tolower_table[i] = -1;
                g_ctype_toupper_table[i] = -1;
            }
            continue;
        }

        unsigned short flags = 0;
        if (x_isupper(c)) flags |= X_CTYPE_UPPER;
        if (x_islower(c)) flags |= X_CTYPE_LOWER;
        if (x_isalpha(c)) flags |= X_CTYPE_ALPHA;
        if (x_isdigit(c)) flags |= X_CTYPE_DIGIT;
        if (x_isxdigit(c)) flags |= X_CTYPE_XDIGIT;
        if (x_isspace(c)) flags |= X_CTYPE_SPACE;
        if (x_isprint(c)) flags |= X_CTYPE_PRINT;
        if (x_isgraph(c)) flags |= X_CTYPE_GRAPH;
        if (x_isblank(c)) flags |= X_CTYPE_BLANK;
        if (x_iscntrl(c)) flags |= X_CTYPE_CNTRL;
        if (x_isgraph(c) && !x_isalpha(c) && !x_isdigit(c)) flags |= X_CTYPE_PUNCT;
        if (x_isalpha(c) || x_isdigit(c)) flags |= X_CTYPE_ALNUM;

        g_ctype_b_table[i] = flags;
        g_ctype_tolower_table[i] = x_tolower(c);
        g_ctype_toupper_table[i] = x_toupper(c);
    }

    g_ctype_tables_ready = true;
}

extern "C" const unsigned short int **__ctype_b_loc(void)
{
    ensure_ctype_tables();
    return &g_ctype_b_ptr;
}

extern "C" const int32_t **__ctype_tolower_loc(void)
{
    ensure_ctype_tables();
    return &g_ctype_tolower_ptr;
}

extern "C" const int32_t **__ctype_toupper_loc(void)
{
    ensure_ctype_tables();
    return &g_ctype_toupper_ptr;
}

int isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

int isascii(int c)
{
    return (unsigned int)c <= 0x7f;
}

int isblank(int c)
{
    return c == ' ' || c == '\t';
}

int iscntrl(int c)
{
    return (c >= 0 && c < 0x20) || c == 0x7f;
}

int isgraph(int c)
{
    return c > ' ' && c <= '~';
}

int islower(int c)
{
    return c >= 'a' && c <= 'z';
}

int isprint(int c)
{
    return c >= ' ' && c <= '~';
}

int ispunct(int c)
{
    return isgraph(c) && !isalnum(c);
}

int isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

int isxdigit(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

int tolower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }
    return c;
}

int toupper(int c)
{
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    return c;
}
