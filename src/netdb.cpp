#include "./include/x3api.h"
#include "./include/libsys.h"
#include "./include/xposix/netdb.h"

static int xnetdb_parse_ipv4(const char *text, uint32_t *out_addr)
{
    uint32_t parts[4] = {0, 0, 0, 0};
    int      part     = 0;
    uint32_t value    = 0;

    if (text == NULL || out_addr == NULL) {
        return 0;
    }

    for (const char *p = text;; ++p) {
        char ch = *p;
        if (ch >= '0' && ch <= '9') {
            value = value * 10 + (uint32_t)(ch - '0');
            if (value > 255) {
                return 0;
            }
        } else if (ch == '.' || ch == '\0') {
            if (part >= 4) {
                return 0;
            }
            parts[part++] = value;
            value         = 0;
            if (ch == '\0') {
                break;
            }
        } else {
            return 0;
        }
    }

    if (part != 4) {
        return 0;
    }

    *out_addr = htonl((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]);
    return 1;
}

static int xnetdb_parse_ipv4_bytes(const char *text, uint8_t out[4])
{
    uint32_t addr = 0;
    uint32_t host = 0;
    if (!xnetdb_parse_ipv4(text, &addr) || out == NULL) {
        return 0;
    }

    host = ntohl(addr);
    out[0] = (uint8_t)((host >> 24) & 0xffU);
    out[1] = (uint8_t)((host >> 16) & 0xffU);
    out[2] = (uint8_t)((host >> 8) & 0xffU);
    out[3] = (uint8_t)(host & 0xffU);
    return 1;
}

static int xnetdb_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int xnetdb_parse_ipv6(const char *text, uint8_t out[16])
{
    uint16_t    words[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int         word_count = 0;
    int         compress_at = -1;
    const char *p;
    const char *end;

    if (text == NULL || out == NULL) {
        return 0;
    }

    p = text;
    end = text + strlen(text);
    if ((end - p) >= 2 && p[0] == '[' && end[-1] == ']') {
        ++p;
        --end;
    }
    if (p == end) {
        return 0;
    }

    if (*p == ':') {
        if (p + 1 >= end || p[1] != ':') {
            return 0;
        }
        compress_at = 0;
        p += 2;
    }

    while (p < end) {
        if (word_count >= 8) {
            return 0;
        }

        const char *segment_start = p;
        while (p < end && *p != ':') {
            ++p;
        }

        const char *dot = NULL;
        for (const char *cursor = segment_start; cursor < p; ++cursor) {
            if (*cursor == '.') {
                dot = cursor;
                break;
            }
        }

        if (dot != NULL) {
            if (word_count > 6) {
                return 0;
            }

            uint8_t ipv4_bytes[4];
            char    tail[32];
            size_t  tail_len = (size_t)(end - segment_start);
            if (tail_len == 0 || tail_len >= sizeof(tail)) {
                return 0;
            }
            memcpy(tail, segment_start, tail_len);
            tail[tail_len] = '\0';
            if (!xnetdb_parse_ipv4_bytes(tail, ipv4_bytes)) {
                return 0;
            }

            words[word_count++] = (uint16_t)(((uint16_t)ipv4_bytes[0] << 8) | ipv4_bytes[1]);
            words[word_count++] = (uint16_t)(((uint16_t)ipv4_bytes[2] << 8) | ipv4_bytes[3]);
            p = end;
            break;
        }

        if (segment_start == p) {
            return 0;
        }

        uint32_t value = 0;
        int      digits = 0;
        for (const char *cursor = segment_start; cursor < p; ++cursor) {
            int hex = xnetdb_hex_value(*cursor);
            if (hex < 0) {
                return 0;
            }
            value = (value << 4) | (uint32_t)hex;
            if (++digits > 4) {
                return 0;
            }
        }
        words[word_count++] = (uint16_t)value;

        if (p >= end) {
            break;
        }

        ++p;
        if (p < end && *p == ':') {
            if (compress_at >= 0) {
                return 0;
            }
            compress_at = word_count;
            ++p;
            if (p >= end) {
                break;
            }
        }
    }

    if (compress_at < 0) {
        if (word_count != 8) {
            return 0;
        }
    } else {
        int zeros = 8 - word_count;
        if (zeros < 0) {
            return 0;
        }
        for (int i = word_count - 1; i >= compress_at; --i) {
            words[i + zeros] = words[i];
        }
        for (int i = 0; i < zeros; ++i) {
            words[compress_at + i] = 0;
        }
        word_count = 8;
    }

    for (int i = 0; i < 8; ++i) {
        out[i * 2]     = (uint8_t)(words[i] >> 8);
        out[i * 2 + 1] = (uint8_t)(words[i] & 0xffU);
    }
    return 1;
}

static int xnetdb_parse_service(const char *service, uint16_t *out_port)
{
    if (out_port == NULL) {
        return EAI_SERVICE;
    }
    if (service == NULL || service[0] == '\0') {
        *out_port = 0;
        return 0;
    }

    long port = strtol(service, NULL, 10);
    if (port < 0 || port > 65535) {
        return EAI_SERVICE;
    }

    *out_port = (uint16_t)port;
    return 0;
}

static int xnetdb_query_dnsfs_text(const char *hostname, const char *prefix, char *response, size_t response_size)
{
    static char dnsfs_path[] = "/run/dns/resolve";
    char        request[320];

    if (hostname == NULL || hostname[0] == '\0' || response == NULL || response_size == 0) {
        return EAI_NONAME;
    }

    int fd = open(dnsfs_path, O_RDONLY);
    if (fd < 0) {
        return EAI_FAIL;
    }

    if (prefix != NULL && prefix[0] != '\0') {
        snprintf(request, sizeof(request), "%s %s", prefix, hostname);
    } else {
        snprintf(request, sizeof(request), "%s", hostname);
    }

    int name_len = (int)strlen(request);
    int wrote = write(fd, request, (uint64_t)name_len);
    if (wrote != name_len) {
        close(fd);
        return EAI_FAIL;
    }

    int got = read(fd, response, response_size - 1);
    close(fd);
    if (got <= 0) {
        return EAI_NONAME;
    }

    response[got] = '\0';
    while (got > 0 && (response[got - 1] == '\n' || response[got - 1] == '\r' ||
                       response[got - 1] == ' ' || response[got - 1] == '\t')) {
        response[--got] = '\0';
    }

    return 0;
}

static int xnetdb_query_dnsfs_ipv4(const char *hostname, uint32_t *out_addr)
{
    char response[64];
    if (out_addr == NULL) {
        return EAI_NONAME;
    }
    int status = xnetdb_query_dnsfs_text(hostname, "ipv4", response, sizeof(response));
    if (status != 0) {
        return status;
    }
    if (!xnetdb_parse_ipv4(response, out_addr)) {
        return EAI_FAIL;
    }
    return 0;
}

static int xnetdb_query_dnsfs_ipv6(const char *hostname, uint8_t out_addr[16])
{
    char response[96];
    if (out_addr == NULL) {
        return EAI_NONAME;
    }
    int status = xnetdb_query_dnsfs_text(hostname, "ipv6", response, sizeof(response));
    if (status != 0) {
        return status;
    }
    if (!xnetdb_parse_ipv6(response, out_addr)) {
        return EAI_FAIL;
    }
    return 0;
}

static int xnetdb_alloc_addrinfo_v4(const char *node,
                                    uint16_t port,
                                    const struct addrinfo *hints,
                                    uint32_t addr,
                                    struct addrinfo **res)
{
    struct addrinfo   *info = (struct addrinfo *)calloc(1, sizeof(struct addrinfo));
    struct sockaddr_in *addr_in = (struct sockaddr_in *)calloc(1, sizeof(struct sockaddr_in));
    if (info == NULL || addr_in == NULL) {
        free(info);
        free(addr_in);
        return EAI_MEMORY;
    }

    addr_in->sin_family      = AF_INET;
    addr_in->sin_port        = htons(port);
    addr_in->sin_addr.s_addr = addr;

    info->ai_flags    = hints ? hints->ai_flags : 0;
    info->ai_family   = AF_INET;
    info->ai_socktype = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    info->ai_protocol = hints ? hints->ai_protocol : 0;
    info->ai_addrlen  = sizeof(struct sockaddr_in);
    info->ai_addr     = (struct sockaddr *)addr_in;
    info->ai_next     = NULL;

    if (node != NULL && hints != NULL && (hints->ai_flags & AI_CANONNAME)) {
        info->ai_canonname = (char *)malloc(strlen(node) + 1);
        if (info->ai_canonname == NULL) {
            free(addr_in);
            free(info);
            return EAI_MEMORY;
        }
        strcpy(info->ai_canonname, node);
    }

    *res = info;
    return 0;
}

static void xnetdb_append_result(struct addrinfo **head, struct addrinfo *node)
{
    if (head == NULL || node == NULL) {
        return;
    }
    if (*head == NULL) {
        *head = node;
        return;
    }

    struct addrinfo *tail = *head;
    while (tail->ai_next != NULL) {
        tail = tail->ai_next;
    }
    tail->ai_next = node;
}

static int xnetdb_alloc_addrinfo_v6(const char *node,
                                    uint16_t port,
                                    const struct addrinfo *hints,
                                    const uint8_t addr[16],
                                    struct addrinfo **res)
{
    struct addrinfo    *info = (struct addrinfo *)calloc(1, sizeof(struct addrinfo));
    struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)calloc(1, sizeof(struct sockaddr_in6));
    if (info == NULL || addr_in6 == NULL) {
        free(info);
        free(addr_in6);
        return EAI_MEMORY;
    }

    addr_in6->sin6_family = AF_INET6;
    addr_in6->sin6_port   = htons(port);
    memcpy(addr_in6->sin6_addr.s6_addr, addr, 16);

    info->ai_flags    = hints ? hints->ai_flags : 0;
    info->ai_family   = AF_INET6;
    info->ai_socktype = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    info->ai_protocol = hints ? hints->ai_protocol : 0;
    info->ai_addrlen  = sizeof(struct sockaddr_in6);
    info->ai_addr     = (struct sockaddr *)addr_in6;
    info->ai_next     = NULL;

    if (node != NULL && hints != NULL && (hints->ai_flags & AI_CANONNAME)) {
        info->ai_canonname = (char *)malloc(strlen(node) + 1);
        if (info->ai_canonname == NULL) {
            free(addr_in6);
            free(info);
            return EAI_MEMORY;
        }
        strcpy(info->ai_canonname, node);
    }

    *res = info;
    return 0;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    if (res == NULL) {
        return EAI_FAIL;
    }
    *res = NULL;

    if (hints != NULL) {
        if (hints->ai_family != 0 && hints->ai_family != AF_INET &&
            hints->ai_family != AF_INET6 && hints->ai_family != AF_UNSPEC) {
            return EAI_FAMILY;
        }
        if (hints->ai_socktype != 0 && hints->ai_socktype != SOCK_STREAM) {
            return EAI_SOCKTYPE;
        }
        if ((hints->ai_flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST)) != 0) {
            return EAI_BADFLAGS;
        }
    }

    uint16_t port = 0;
    int service_status = xnetdb_parse_service(service, &port);
    if (service_status != 0) {
        return service_status;
    }

    int      requested_family = hints ? hints->ai_family : AF_UNSPEC;
    uint32_t addr4 = 0;
    uint8_t  addr6[16];
    memset(addr6, 0, sizeof(addr6));

    if (node == NULL || node[0] == '\0') {
        if (requested_family == AF_INET6) {
            if (hints != NULL && (hints->ai_flags & AI_PASSIVE)) {
                memset(addr6, 0, sizeof(addr6));
            } else {
                addr6[15] = 1;
            }
            return xnetdb_alloc_addrinfo_v6(node, port, hints, addr6, res);
        }

        addr4 = (hints != NULL && (hints->ai_flags & AI_PASSIVE)) ? 0 : htonl(0x7f000001U);
        return xnetdb_alloc_addrinfo_v4(node, port, hints, addr4, res);
    }

    if (xnetdb_parse_ipv6(node, addr6)) {
        if (requested_family == AF_INET) {
            return EAI_NONAME;
        }
        return xnetdb_alloc_addrinfo_v6(node, port, hints, addr6, res);
    }

    if (xnetdb_parse_ipv4(node, &addr4)) {
        if (requested_family == AF_INET6) {
            return EAI_NONAME;
        }
        return xnetdb_alloc_addrinfo_v4(node, port, hints, addr4, res);
    }

    if (hints != NULL && (hints->ai_flags & AI_NUMERICHOST)) {
        return EAI_NONAME;
    }
    if (requested_family == AF_INET6) {
        int dns6_status = xnetdb_query_dnsfs_ipv6(node, addr6);
        if (dns6_status != 0) {
            return dns6_status;
        }
        return xnetdb_alloc_addrinfo_v6(node, port, hints, addr6, res);
    }

    if (requested_family == AF_INET) {
        int dns4_status = xnetdb_query_dnsfs_ipv4(node, &addr4);
        if (dns4_status != 0) {
            return dns4_status;
        }
        return xnetdb_alloc_addrinfo_v4(node, port, hints, addr4, res);
    }

    struct addrinfo *head = NULL;
    int dns6_status = xnetdb_query_dnsfs_ipv6(node, addr6);
    if (dns6_status == 0) {
        struct addrinfo *node6 = NULL;
        int alloc6_status = xnetdb_alloc_addrinfo_v6(node, port, hints, addr6, &node6);
        if (alloc6_status != 0) {
            return alloc6_status;
        }
        xnetdb_append_result(&head, node6);
    }

    int dns4_status = xnetdb_query_dnsfs_ipv4(node, &addr4);
    if (dns4_status == 0) {
        struct addrinfo *node4 = NULL;
        int alloc4_status = xnetdb_alloc_addrinfo_v4(node, port, hints, addr4, &node4);
        if (alloc4_status != 0) {
            freeaddrinfo(head);
            return alloc4_status;
        }
        xnetdb_append_result(&head, node4);
    }

    if (head == NULL) {
        return dns6_status == 0 ? dns4_status : (dns4_status == 0 ? dns6_status : dns6_status);
    }

    *res = head;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    while (res != NULL) {
        struct addrinfo *next = res->ai_next;
        free(res->ai_addr);
        free(res->ai_canonname);
        free(res);
        res = next;
    }
}

const char *gai_strerror(int errcode)
{
    switch (errcode) {
    case 0: return "success";
    case EAI_BADFLAGS: return "bad flags";
    case EAI_NONAME: return "name or service not known";
    case EAI_AGAIN: return "temporary failure";
    case EAI_FAIL: return "non-recoverable failure";
    case EAI_FAMILY: return "address family not supported";
    case EAI_SOCKTYPE: return "socket type not supported";
    case EAI_SERVICE: return "service not supported";
    case EAI_MEMORY: return "out of memory";
    default: return "unknown error";
    }
}

struct hostent *gethostbyname(const char *name)
{
    static struct hostent host;
    static char          *aliases[1] = {NULL};
    static char          *addr_list[2] = {NULL, NULL};
    static char           canonical_name[256];
    static struct in_addr addr;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }

    if (!xnetdb_parse_ipv4(name, &addr.s_addr)) {
        if (xnetdb_query_dnsfs_ipv4(name, &addr.s_addr) != 0) {
            return NULL;
        }
    }

    strncpy(canonical_name, name, sizeof(canonical_name) - 1);
    canonical_name[sizeof(canonical_name) - 1] = '\0';

    host.h_name      = canonical_name;
    host.h_aliases   = aliases;
    host.h_addrtype  = AF_INET;
    host.h_length    = sizeof(struct in_addr);
    addr_list[0]     = (char *)&addr;
    host.h_addr_list = addr_list;
    return &host;
}
