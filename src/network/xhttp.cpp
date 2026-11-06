// SPDX-License-Identifier: Apache-2.0
#include "internal.h"

namespace xnet_sdk {
namespace {
struct Url
{
    bool     secure, ipv6;
    uint16_t port;
    char     host[254], path[XHTTP_MAX_URL_BYTES];
};
struct OwnedHeader
{
    char *name, *value;
};
struct ResponseHeader
{
    const char *name, *value;
};
struct Http : Object
{
    Url               url;
    Dial              dial;
    XTlsHandle        tls;
    XTlsPeerInfo      tls_info;
    XHttpResponseInfo info;
    uint64_t          deadline, limit, body_size, body_sent, content_remaining, chunk_remaining;
    uint32_t          redirect_limit, request_count, wire_bytes, wire_sent;
    char              method[33];
    OwnedHeader       request_headers[XHTTP_MAX_HEADERS];
    unsigned char    *body, *ca_data;
    uint32_t          ca_bytes;
    char             *ca_file, *wire;
    unsigned char     input[8192];
    uint32_t          begin, end, line_bytes, storage_bytes, total_header_bytes, response_count, interim_count;
    char              line[8193], storage[XHTTP_MAX_HEADER_BYTES];
    ResponseHeader    response_headers[XHTTP_MAX_HEADERS];
    bool     transport_eof, want_write, headers_ready, status_read, length_seen, transfer_seen, chunked, no_body;
    bool     reuse, pool_checked, http11, response_close;
    uint32_t chunk_phase, trailer_bytes, trailer_count;
};

struct IdleConnection
{
    Url url;
    Dial dial;
    XTlsHandle tls;
    uint64_t expires;
};
// Only idle transports are shared. Callers already passed same_process(),
// so forked/copied SDK state is rejected before touching this pool lock.
constexpr unsigned pool_capacity = 4;
constexpr uint64_t pool_idle_ms = 15000;
IdleConnection idle_pool[pool_capacity];
int pool_mutex;

void pool_lock()
{
    while (__atomic_exchange_n(&pool_mutex, 1, __ATOMIC_ACQUIRE)) raw_call(XAPI_SLEEP, 0);
}

void pool_unlock()
{
    __atomic_store_n(&pool_mutex, 0, __ATOMIC_RELEASE);
}

void idle_close(IdleConnection &connection)
{
    if (connection.tls) xapi_TlsClose(connection.tls);
    dial_close(connection.dial);
    connection = {};
}

void pool_clear()
{
    IdleConnection detached[pool_capacity] = {};
    pool_lock();
    for (unsigned i = 0; i < pool_capacity; ++i)
    {
        detached[i] = idle_pool[i];
        idle_pool[i] = {};
    }
    pool_unlock();
    for (auto &connection : detached) idle_close(connection);
}

int64_t receive(Http *http, void *data, uint32_t bytes);

bool transport_idle(Http *http)
{
    unsigned char unexpected;
    // An idle HTTP/1.1 connection has no application bytes. This also lets
    // TLS consume pending control records and detect close_notify/TCP EOF.
    return receive(http, &unexpected, 1) == -EAGAIN;
}

bool pool_take(Http *http)
{
    for (unsigned attempt = 0; attempt < pool_capacity; ++attempt)
    {
        IdleConnection candidate = {};
        uint64_t now = now_ms();
        pool_lock();
        for (auto &entry : idle_pool)
            if (entry.expires && (now >= entry.expires ||
                (entry.url.secure == http->url.secure && entry.url.port == http->url.port &&
                 !strcmp(entry.url.host, http->url.host))))
            {
                candidate = entry;
                entry = {};
                break;
            }
        pool_unlock();
        if (!candidate.expires) return false;
        bool matching = now < candidate.expires && candidate.url.secure == http->url.secure &&
                        candidate.url.port == http->url.port && !strcmp(candidate.url.host, http->url.host);
        if (matching && candidate.tls)
            matching = tls_http_reuse_trust(candidate.tls, http->ca_data, http->ca_bytes, http->ca_file);
        if (!matching)
        {
            idle_close(candidate);
            continue;
        }
        http->tls = candidate.tls;
        http->dial = candidate.dial;
        if (transport_idle(http)) return true;
        // No request bytes have been sent. Reject an already closed/dirty idle
        // candidate before connecting normally; never replay a sent request.
        if (http->tls) xapi_TlsClose(http->tls);
        http->tls = 0;
        dial_close(http->dial);
        http->dial = {};
    }
    return false;
}

void pool_put(Http *http)
{
    IdleConnection connection = {http->url, http->dial, http->tls, now_ms() + pool_idle_ms};
    IdleConnection evicted = {};
    pool_lock();
    unsigned slot = 0;
    for (unsigned i = 0; i < pool_capacity; ++i)
        if (!idle_pool[i].expires)
        {
            slot = i;
            break;
        }
        else if (idle_pool[i].expires < idle_pool[slot].expires) slot = i;
    evicted = idle_pool[slot];
    idle_pool[slot] = connection;
    http->tls = 0;
    http->dial = {};
    pool_unlock();
    idle_close(evicted);
}

bool token_character(unsigned char ch)
{
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) return true;
    return ch && strchr("!#$%&'*+-.^_`|~", ch) != NULL;
}

bool valid_value(const char *text)
{
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        if ((*p < 32 && *p != '\t') || *p == 127) return false;
    return true;
}

bool prefix(const char *text, const char *value)
{
    while (*value)
        if (ascii_lower(*text++) != ascii_lower(*value++)) return false;
    return true;
}

int parse_url(const char *text, Url *out)
{
    if (!text) return -EINVAL;
    uint32_t size = 0;
    while (size < XHTTP_MAX_URL_BYTES && text[size])
    {
        unsigned char ch = text[size++];
        if (ch <= 32 || ch >= 127 || ch == '\\') return -EINVAL;
    }
    if (!size || size == XHTTP_MAX_URL_BYTES) return -EINVAL;
    Url         url = {};
    const char *authority;
    if (prefix(text, "http://"))
    {
        url.port  = 80;
        authority = text + 7;
    }
    else if (prefix(text, "https://"))
    {
        url.secure = true;
        url.port   = 443;
        authority  = text + 8;
    }
    else
        return -EPROTONOSUPPORT;
    const char *end = authority;
    while (*end && *end != '/' && *end != '?' && *end != '#')
        ++end;
    if (end == authority) return -EINVAL;
    for (const char *p = authority; p != end; ++p)
        if (*p == '@') return -EINVAL;
    const char *host_end = end, *port = NULL;
    const char *host = authority;
    if (*host == '[')
    {
        url.ipv6 = true;
        host     = authority + 1;
        host_end = host;
        while (host_end != end && *host_end != ']')
            ++host_end;
        if (host_end == end || host_end == host) return -EINVAL;
        if (host_end + 1 != end)
        {
            if (host_end[1] != ':') return -EINVAL;
            port = host_end + 2;
        }
    }
    else
    {
        for (const char *p = host; p != end; ++p)
            if (*p == ':')
            {
                if (port) return -EINVAL;
                host_end = p;
                port     = p + 1;
            }
    }
    size_t host_size = host_end - host;
    if (!host_size || host_size >= sizeof(url.host)) return -EINVAL;
    for (size_t i = 0; i < host_size; ++i)
    {
        char ch = ascii_lower(host[i]);
        if (!url.ipv6 && !((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.'))
            return -EINVAL;
        url.host[i] = ch;
    }
    if (url.ipv6)
    {
        XNetAddress address;
        if (xapi_NetParseAddress(url.host, &address) || address.family != XNET_IPV6) return -EINVAL;
    }
    if (port)
    {
        if (port == end) return -EINVAL;
        unsigned value = 0;
        for (const char *p = port; p != end; ++p)
        {
            if (*p < '0' || *p > '9') return -EINVAL;
            value = value * 10 + unsigned(*p - '0');
            if (value > 65535) return -EINVAL;
        }
        if (!value) return -EINVAL;
        url.port = uint16_t(value);
    }
    const char *fragment = end;
    while (*fragment && *fragment != '#')
        ++fragment;
    size_t path_size = fragment - end;
    size_t offset    = *end == '/' ? 0 : 1;
    if (path_size + offset >= sizeof(url.path)) return -ENAMETOOLONG;
    if (offset) url.path[0] = '/';
    memcpy(url.path + offset, end, path_size);
    *out = url;
    return 0;
}

struct Writer
{
    char    *data;
    uint32_t capacity, used;
    bool     ok;
    void     append(const char *text)
    {
        size_t n = strlen(text);
        if (!ok || n >= capacity - used)
        {
            ok = false;
            return;
        }
        memcpy(data + used, text, n);
        used       += uint32_t(n);
        data[used]  = 0;
    }
    void number(uint64_t value)
    {
        char     digits[21];
        unsigned used_digits = 0;
        do
        {
            digits[used_digits++]  = char('0' + value % 10);
            value                 /= 10;
        } while (value);
        char forward[21];
        for (unsigned i = 0; i < used_digits; ++i)
            forward[i] = digits[used_digits - i - 1];
        forward[used_digits] = 0;
        append(forward);
    }
};

bool append_origin(Writer &writer, const Url &url)
{
    writer.append(url.secure ? "https://" : "http://");
    if (url.ipv6) writer.append("[");
    writer.append(url.host);
    if (url.ipv6) writer.append("]");
    if (url.port != (url.secure ? 443 : 80))
    {
        writer.append(":");
        writer.number(url.port);
    }
    return writer.ok;
}

int serialize(const Url &url, char *out)
{
    Writer writer = {out, XHTTP_MAX_URL_BYTES, 0, true};
    append_origin(writer, url);
    writer.append(url.path);
    return writer.ok ? 0 : -ENAMETOOLONG;
}

void snapshot_tls(Http *http)
{
    if (http->tls)
    {
        http->tls_info.size    = sizeof(http->tls_info);
        http->tls_info.version = 1;
        xapi_TlsGetPeerInfo(http->tls, &http->tls_info);
    }
}

void transport_close(Http *http, bool snapshot = true)
{
    if (snapshot) snapshot_tls(http);
    if (http->tls) xapi_TlsClose(http->tls);
    http->tls = 0;
    dial_close(http->dial);
}

void destroy(Object *base)
{
    auto *http = static_cast<Http *>(base);
    transport_close(http);
    if (http->cancel) xapi_NetClose(http->cancel);
    for (OwnedHeader &header : http->request_headers)
    {
        free(header.name);
        free(header.value);
    }
    free(http->body);
    free(http->ca_data);
    free(http->ca_file);
    free(http->wire);
    free(http);
}

int fail(Http *http, int error)
{
    if (http->info.state != XHTTP_FAILED)
    {
        http->info.state  = XHTTP_FAILED;
        http->info.result = error;
        transport_close(http);
    }
    return int(http->info.result);
}

void complete(Http *http)
{
    http->info.state  = XHTTP_COMPLETE;
    http->info.result = 0;
    snapshot_tls(http);
    bool framed = http->length_seen || http->chunked || http->no_body;
    if (http->no_body && (http->chunked ||
        ((http->info.status_code == 204 || http->info.status_code == 205) && http->info.content_length)))
        framed = false;
    if (http->reuse && http->http11 && framed && !http->response_close && !http->transport_eof &&
        http->begin == http->end && !cancelled(http) &&
        (!http->deadline || now_ms() < http->deadline) && transport_idle(http))
        pool_put(http);
    else
        // A completed framed response remains valid if an idle health probe
        // discovers EOF. Keep the successful request's TLS snapshot intact.
        transport_close(http, false);
}

int check(Http *http, Budget *budget = NULL)
{
    if (http->info.state == XHTTP_FAILED) return int(http->info.result);
    if (int error = cancelled(http, budget)) return fail(http, error);
    if (http->info.state != XHTTP_COMPLETE && http->deadline && now_ms() >= http->deadline)
        return fail(http, -ETIMEDOUT);
    return 0;
}

bool framing_header(const char *name)
{
    return equal_ascii(name, "Host") || equal_ascii(name, "Connection") || equal_ascii(name, "Content-Length") ||
           equal_ascii(name, "Transfer-Encoding") || equal_ascii(name, "Expect") || equal_ascii(name, "Upgrade") ||
           equal_ascii(name, "Trailer");
}

int build_request(Http *http)
{
    free(http->wire);
    const uint32_t capacity = XHTTP_MAX_HEADER_BYTES + XHTTP_MAX_URL_BYTES + 1024;
    http->wire              = static_cast<char *>(malloc(capacity));
    if (!http->wire) return -ENOMEM;
    Writer writer = {http->wire, capacity, 0, true};
    writer.append(http->method);
    writer.append(" ");
    writer.append(http->url.path);
    writer.append(" HTTP/1.1\r\nHost: ");
    if (http->url.ipv6) writer.append("[");
    char host[sizeof(http->url.host)];
    memcpy(host, http->url.host, sizeof(host));
    // The zone is local routing metadata, never part of the wire authority.
    char *zone = strchr(host, '%');
    if (zone) *zone = 0;
    writer.append(host);
    if (http->url.ipv6) writer.append("]");
    if (http->url.port != (http->url.secure ? 443 : 80))
    {
        writer.append(":");
        writer.number(http->url.port);
    }
    writer.append(http->reuse ? "\r\nConnection: keep-alive\r\nContent-Length: "
                              : "\r\nConnection: close\r\nContent-Length: ");
    writer.number(http->body_size);
    writer.append("\r\n");
    for (OwnedHeader &header : http->request_headers)
        if (header.name)
        {
            writer.append(header.name);
            writer.append(": ");
            writer.append(header.value);
            writer.append("\r\n");
        }
    writer.append("\r\n");
    if (!writer.ok) return -EMSGSIZE;
    http->wire_bytes = writer.used;
    http->wire_sent  = 0;
    http->body_sent  = 0;
    return 0;
}

int64_t send(Http *http, const void *data, uint32_t bytes)
{
    http->want_write = true;
    XNetIoOptions io = {sizeof(io), 1, 0, 0, http->cancel};
    return http->tls ? xapi_TlsSend(http->tls, data, bytes, &io) : xapi_NetSend(http->dial.socket, data, bytes, &io);
}

int64_t receive(Http *http, void *data, uint32_t bytes)
{
    http->want_write = false;
    XNetIoOptions io = {sizeof(io), 1, 0, 0, http->cancel};
    return http->tls ? xapi_TlsReceive(http->tls, data, bytes, &io)
                     : xapi_NetReceive(http->dial.socket, data, bytes, &io);
}

int fill(Http *http)
{
    if (http->begin < http->end) return 1;
    if (http->transport_eof) return 2;
    int64_t result = receive(http, http->input, sizeof(http->input));
    if (result == -EAGAIN) return 0;
    if (result < 0) return fail(http, int(result));
    http->begin = 0;
    http->end   = uint32_t(result);
    if (!result)
    {
        http->transport_eof = true;
        return 2;
    }
    return 1;
}

int line(Http *http)
{
    for (;;)
    {
        int result = fill(http);
        if (result <= 0) return result;
        if (result == 2) return fail(http, -EPROTO);
        unsigned char ch = http->input[http->begin++];
        if (ch == '\n')
        {
            if (!http->line_bytes || http->line[http->line_bytes - 1] != '\r') return fail(http, -EPROTO);
            http->line[--http->line_bytes] = 0;
            return 1;
        }
        if (http->line_bytes >= sizeof(http->line) - 1) return fail(http, -EMSGSIZE);
        if (ch == 0 || (ch < 32 && ch != '\r' && ch != '\t') || ch == 127) return fail(http, -EPROTO);
        if (http->line_bytes && http->line[http->line_bytes - 1] == '\r') return fail(http, -EPROTO);
        http->line[http->line_bytes++] = char(ch);
    }
}

bool decimal_length(const char *text, uint64_t *out)
{
    if (!*text) return false;
    uint64_t value = 0;
    while (*text)
    {
        if (*text < '0' || *text > '9') return false;
        unsigned digit = unsigned(*text++ - '0');
        if (value > (~uint64_t(0) - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

bool header_token(const char *text, const char *token)
{
    while (*text)
    {
        while (*text == ',' || *text == ' ' || *text == '\t') ++text;
        const char *begin = text;
        while (*text && *text != ',') ++text;
        const char *end = text;
        while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) --end;
        size_t length = size_t(end - begin);
        if (length == strlen(token))
        {
            size_t i = 0;
            while (i < length && ascii_lower(begin[i]) == token[i]) ++i;
            if (i == length) return true;
        }
    }
    return false;
}

bool connection_tokens_valid(const char *text)
{
    for (;;)
    {
        while (*text == ' ' || *text == '\t') ++text;
        const char *begin = text;
        while (token_character(static_cast<unsigned char>(*text))) ++text;
        if (text == begin) return false;
        while (*text == ' ' || *text == '\t') ++text;
        if (!*text) return true;
        if (*text++ != ',') return false;
    }
}

int split_header(char *text, char **name, char **value)
{
    if (!text[0] || text[0] == ' ' || text[0] == '\t') return -EPROTO;
    char *colon = text;
    while (*colon && *colon != ':')
    {
        if (!token_character(unsigned(*colon))) return -EPROTO;
        ++colon;
    }
    if (*colon != ':' || colon == text) return -EPROTO;
    *colon++ = 0;
    while (*colon == ' ' || *colon == '\t')
        ++colon;
    if (!valid_value(colon)) return -EPROTO;
    char *end = colon + strlen(colon);
    while (end != colon && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = 0;
    *name  = text;
    *value = colon;
    return 0;
}

int parse_header(Http *http)
{
    char *name, *value;
    int   error = split_header(http->line, &name, &value);
    if (error) return fail(http, error);
    if (http->response_count == XHTTP_MAX_HEADERS) return fail(http, -EMSGSIZE);
    size_t name_bytes = strlen(name) + 1, value_bytes = strlen(value) + 1;
    if (name_bytes + value_bytes > sizeof(http->storage) - http->storage_bytes) return fail(http, -EMSGSIZE);
    char *copy = http->storage + http->storage_bytes;
    memcpy(copy, name, name_bytes);
    memcpy(copy + name_bytes, value, value_bytes);
    http->response_headers[http->response_count++]  = {copy, copy + name_bytes};
    http->storage_bytes                            += uint32_t(name_bytes + value_bytes);
    if (equal_ascii(name, "Content-Length"))
    {
        uint64_t length;
        if (!decimal_length(value, &length) || (http->length_seen && length != http->info.content_length))
            return fail(http, -EPROTO);
        http->length_seen         = true;
        http->info.content_length = length;
    }
    if (equal_ascii(name, "Transfer-Encoding"))
    {
        if (http->transfer_seen || !equal_ascii(value, "chunked")) return fail(http, -EPROTO);
        http->transfer_seen = http->chunked = true;
    }
    if (equal_ascii(name, "Connection") &&
        (!connection_tokens_valid(value) || header_token(value, "close") || header_token(value, "upgrade")))
        http->response_close = true;
    return 0;
}

const char *header_value(const Http *http, const char *name, uint32_t index)
{
    for (uint32_t i = 0; i < http->response_count; ++i)
        if (equal_ascii(http->response_headers[i].name, name))
        {
            if (!index) return http->response_headers[i].value;
            --index;
        }
    return NULL;
}

void reset_response(Http *http)
{
    http->begin = http->end = http->line_bytes = http->storage_bytes = http->total_header_bytes = 0;
    http->response_count = http->interim_count = http->trailer_bytes = http->trailer_count = 0;
    http->transport_eof = http->headers_ready = http->status_read = http->length_seen = false;
    http->transfer_seen = http->chunked = http->no_body = false;
    http->http11 = http->response_close = false;
    http->content_remaining = http->chunk_remaining = 0;
    http->chunk_phase                               = 0;
    http->info.status_code = http->info.flags = http->info.header_count = 0;
    http->info.content_length = http->info.body_bytes = 0;
}

void remove_segment(char *out, uint32_t *used)
{
    while (*used && out[*used - 1] != '/')
        --*used;
    if (*used) --*used;
}

int normalize_path(const char *source, char *out)
{
    const char *end = source;
    while (*end && *end != '?' && *end != '#')
        ++end;
    const char *p    = source;
    uint32_t    used = 0;
    while (p != end)
    {
        size_t left = end - p;
        if (left >= 3 && !memcmp(p, "../", 3))
        {
            p += 3;
            continue;
        }
        if (left >= 2 && !memcmp(p, "./", 2))
        {
            p += 2;
            continue;
        }
        if (left >= 3 && !memcmp(p, "/./", 3))
        {
            p += 2;
            continue;
        }
        if (left == 2 && !memcmp(p, "/.", 2))
        {
            p           += 2;
            out[used++]  = '/';
            continue;
        }
        if (left >= 4 && !memcmp(p, "/../", 4))
        {
            p += 3;
            remove_segment(out, &used);
            continue;
        }
        if (left == 3 && !memcmp(p, "/..", 3))
        {
            p += 3;
            remove_segment(out, &used);
            out[used++] = '/';
            continue;
        }
        if ((left == 1 && p[0] == '.') || (left == 2 && !memcmp(p, "..", 2)))
        {
            p = end;
            continue;
        }
        if (used >= XHTTP_MAX_URL_BYTES - 1) return -ENAMETOOLONG;
        out[used++] = *p++;
        while (p != end && *p != '/')
        {
            if (used >= XHTTP_MAX_URL_BYTES - 1) return -ENAMETOOLONG;
            out[used++] = *p++;
        }
    }
    if (!used) out[used++] = '/';
    while (*end && *end != '#')
    {
        if (used >= XHTTP_MAX_URL_BYTES - 1) return -ENAMETOOLONG;
        out[used++] = *end++;
    }
    out[used] = 0;
    return 0;
}

int resolve_location(const Url &base, const char *location, Url *out)
{
    if (!location || !*location) return -EPROTO;
    size_t bytes = strlen(location);
    if (bytes >= XHTTP_MAX_URL_BYTES) return -ENAMETOOLONG;
    char temporary[XHTTP_MAX_URL_BYTES] = {};
    if (prefix(location, "http://") || prefix(location, "https://")) return parse_url(location, out);
    if (location[0] == '/' && location[1] == '/')
    {
        Writer writer = {temporary, sizeof(temporary), 0, true};
        writer.append(base.secure ? "https:" : "http:");
        writer.append(location);
        return writer.ok ? parse_url(temporary, out) : -ENAMETOOLONG;
    }
    for (const char *p = location; *p && *p != '/' && *p != '?' && *p != '#'; ++p)
        if (*p == ':') return -EPROTONOSUPPORT;
    Url    next   = base;
    Writer writer = {temporary, sizeof(temporary), 0, true};
    if (location[0] == '/')
        writer.append(location);
    else
    {
        char path[XHTTP_MAX_URL_BYTES];
        memcpy(path, base.path, sizeof(path));
        char *query = strchr(path, '?');
        if (location[0] == '#')
            writer.append(path);
        else if (location[0] == '?')
        {
            if (query) *query = 0;
            writer.append(path);
        }
        else
        {
            if (query) *query = 0;
            char *slash = strrchr(path, '/');
            if (slash)
                slash[1] = 0;
            else
                path[0] = 0;
            writer.append(path);
        }
        writer.append(location);
    }
    if (!writer.ok) return -ENAMETOOLONG;
    int error = normalize_path(temporary, next.path);
    if (error) return error;
    // Re-parse a complete URL to apply the same control/authority validation.
    error = serialize(next, temporary);
    return error ? error : parse_url(temporary, out);
}

int follow_redirect(Http *http)
{
    uint32_t status = http->info.status_code;
    if (!http->redirect_limit || (status != 301 && status != 302 && status != 303 && status != 307 && status != 308))
        return 0;
    const char *location = header_value(http, "Location", 0);
    if (!location) return 0;
    if (http->info.redirects >= http->redirect_limit) return fail(http, -ELOOP);
    Url next;
    int error = resolve_location(http->url, location, &next);
    if (error) return fail(http, error);
    if (http->url.secure && !next.secure) return fail(http, -EACCES);
    bool cross_origin =
        http->url.secure != next.secure || http->url.port != next.port || strcmp(http->url.host, next.host) != 0;
    bool drop_body = (status == 303 && strcmp(http->method, "HEAD")) ||
                     ((status == 301 || status == 302) && !strcmp(http->method, "POST"));
    for (OwnedHeader &header : http->request_headers)
        if (header.name && ((cross_origin &&
                             (equal_ascii(header.name, "Authorization") ||
                              equal_ascii(header.name, "Proxy-Authorization") || equal_ascii(header.name, "Cookie"))) ||
                            (drop_body && equal_ascii(header.name, "Content-Type"))))
        {
            free(header.name);
            free(header.value);
            header = {};
        }
    if (drop_body)
    {
        strcpy(http->method, "GET");
        free(http->body);
        http->body      = NULL;
        http->body_size = 0;
    }
    transport_close(http);
    http->dial = {};
    http->pool_checked = false;
    http->url  = next;
    ++http->info.redirects;
    reset_response(http);
    error = serialize(http->url, http->info.final_url);
    if (!error) error = build_request(http);
    if (error) return fail(http, error);
    http->info.state = XHTTP_CONNECTING;
    return 1;
}

int finish_headers(Http *http)
{
    if (http->length_seen && http->transfer_seen) return fail(http, -EPROTO);
    if (http->info.status_code >= 100 && http->info.status_code < 200)
    {
        if (http->info.status_code == 101 || ++http->interim_count > 8) return fail(http, -EPROTO);
        http->status_read = http->length_seen = http->transfer_seen = http->chunked = false;
        http->response_count = http->storage_bytes = 0;
        http->response_close = false;
        http->info.status_code                     = 0;
        http->info.content_length                  = 0;
        return 1;
    }
    int redirected = follow_redirect(http);
    if (redirected) return redirected;
    http->headers_ready     = true;
    http->info.header_count = http->response_count;
    if (http->length_seen) http->info.flags |= XHTTP_LENGTH_KNOWN;
    if (http->chunked) http->info.flags |= XHTTP_CHUNKED;
    http->no_body = !strcmp(http->method, "HEAD") || http->info.status_code == 204 || http->info.status_code == 205 ||
                    http->info.status_code == 304;
    http->content_remaining = http->info.content_length;
    if (http->no_body || (http->length_seen && !http->content_remaining))
    {
        complete(http);
        return 1;
    }
    if (http->length_seen && http->limit && http->info.content_length > http->limit) return fail(http, -EMSGSIZE);
    http->info.state = XHTTP_READING_BODY;
    return 1;
}

int read_headers(Http *http)
{
    for (;;)
    {
        int result = line(http);
        if (result <= 0) return result;
        uint32_t length  = http->line_bytes;
        http->line_bytes = 0;
        if (length + 2 > XHTTP_MAX_HEADER_BYTES - http->total_header_bytes) return fail(http, -EMSGSIZE);
        http->total_header_bytes += length + 2;
        if (!http->status_read)
        {
            if (length < 12 || (memcmp(http->line, "HTTP/1.1 ", 9) && memcmp(http->line, "HTTP/1.0 ", 9)) ||
                http->line[9] < '1' || http->line[9] > '5' || http->line[10] < '0' || http->line[10] > '9' ||
                http->line[11] < '0' || http->line[11] > '9' || (length > 12 && http->line[12] != ' ') ||
                !valid_value(http->line))
                return fail(http, -EPROTO);
            http->info.status_code =
                uint32_t((http->line[9] - '0') * 100 + (http->line[10] - '0') * 10 + http->line[11] - '0');
            http->http11 = http->line[7] == '1';
            http->status_read = true;
        }
        else if (!length)
            return finish_headers(http);
        else if (int error = parse_header(http))
            return error;
    }
}

int step(Http *http)
{
    if (int error = check(http)) return error;
    if (http->info.state == XHTTP_COMPLETE || http->info.state == XHTTP_READING_BODY) return 1;
    if (http->info.state == XHTTP_CONNECTING)
    {
        if (http->reuse && !http->pool_checked)
        {
            http->pool_checked = true;
            if (pool_take(http))
            {
                snapshot_tls(http);
                http->info.state = XHTTP_SENDING;
                return 1;
            }
        }
        if (http->url.secure)
        {
            if (!http->tls)
            {
                uint64_t now = now_ms();
                if (http->deadline && now >= http->deadline) return fail(http, -ETIMEDOUT);
                uint64_t remaining = http->deadline ? http->deadline - now : 30000;
                if (remaining > 0x7fffffffU) remaining = 0x7fffffffU;
                XTlsConnectDesc desc = {
                    sizeof(desc),   1,
                    http->url.host, NULL,
                    http->url.port, uint32_t(remaining ? remaining : 1),
                    http->ca_data,  http->ca_bytes,
                    // A pooled TLS connection outlives this request. The
                    // request token is supplied separately on every TLS I/O.
                    http->ca_file,  http->reuse ? 0 : http->cancel,
                    {0, 0}
                };
                int result = int(xapi_TlsConnectStart(&desc, &http->tls));
                if (result) return fail(http, result);
                if (http->reuse && (result = tls_http_enable_reuse(http->tls))) return fail(http, result);
            }
            uint32_t events        = 0;
            XNetIoOptions io       = {sizeof(io), 1, 0, 0, http->cancel};
            int      result        = int(xapi_TlsWait(http->tls, XTLS_EVENT_READY, &io, &events));
            http->tls_info.size    = sizeof(http->tls_info);
            http->tls_info.version = 1;
            xapi_TlsGetPeerInfo(http->tls, &http->tls_info);
            if (result < 0) return fail(http, result);
            if (events & XTLS_ERROR) return fail(http, http->tls_info.result ? int(http->tls_info.result) : -EPROTO);
            if (!(events & XTLS_EVENT_READY)) return 0;
        }
        else
        {
            int result = dial_step(http, http->dial, http->url.host, http->url.port, http->deadline);
            if (result < 0) return fail(http, result);
            if (http->dial.phase != 5) return result;
        }
        http->info.state = XHTTP_SENDING;
        return 1;
    }
    if (http->info.state == XHTTP_SENDING)
    {
        int64_t result;
        if (http->wire_sent < http->wire_bytes)
        {
            result = send(http, http->wire + http->wire_sent, http->wire_bytes - http->wire_sent);
            if (result > 0) http->wire_sent += uint32_t(result);
        }
        else if (http->body_sent < http->body_size)
        {
            uint64_t remaining = http->body_size - http->body_sent;
            if (remaining > XNET_MAX_TRANSFER_BYTES) remaining = XNET_MAX_TRANSFER_BYTES;
            result = send(http, http->body + http->body_sent, uint32_t(remaining));
            if (result > 0) http->body_sent += uint64_t(result);
        }
        else
        {
            http->info.state = XHTTP_READING_HEADERS;
            http->want_write = false;
            return 1;
        }
        if (result == -EAGAIN) return 0;
        if (result <= 0) return fail(http, result < 0 ? int(result) : -EIO);
        return 1;
    }
    return read_headers(http);
}

int await(Http *http, Budget &budget)
{
    if (budget.expired()) return -ETIMEDOUT;
    if (int error = check(http, &budget)) return error;
    if (http->tls)
    {
        int32_t remaining = budget.remaining();
        if (remaining < 0 || remaining > 50) remaining = 50;
        uint64_t now = now_ms();
        if (http->deadline && now >= http->deadline) return -ETIMEDOUT;
        if (http->deadline && uint64_t(remaining) > http->deadline - now) remaining = int32_t(http->deadline - now);
        XNetIoOptions io     = {sizeof(io), 1, remaining, 0, http->cancel};
        uint32_t      events = 0;
        uint32_t      mask   = http->info.state == XHTTP_CONNECTING ? XTLS_EVENT_READY
                               : http->want_write                   ? XTLS_WRITABLE
                                                                    : XTLS_READABLE;
        int           result = int(xapi_TlsWait(http->tls, mask, &io, &events));
        if (int error = check(http, &budget)) return error;
        if (result < 0) return result;
        if (events & XTLS_ERROR)
        {
            http->tls_info.size    = sizeof(http->tls_info);
            http->tls_info.version = 1;
            xapi_TlsGetPeerInfo(http->tls, &http->tls_info);
            return http->tls_info.result ? int(http->tls_info.result) : -EPROTO;
        }
        return result;
    }
    uint32_t   mask;
    XNetHandle handle;
    if (http->dial.phase == 5)
    {
        handle = http->dial.socket;
        mask   = http->want_write ? XNET_WRITABLE : XNET_READABLE;
    }
    else
        handle = dial_interest(http->dial, &mask);
    return wait_native(http, handle, mask, budget, http->deadline);
}

int chunk_size(Http *http)
{
    int result = line(http);
    if (result <= 0) return result;
    http->line_bytes   = 0;
    const char *p      = http->line;
    uint64_t    size   = 0;
    unsigned    digits = 0;
    while (*p && *p != ';')
    {
        unsigned digit;
        if (*p >= '0' && *p <= '9')
            digit = unsigned(*p - '0');
        else if (*p >= 'a' && *p <= 'f')
            digit = unsigned(*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F')
            digit = unsigned(*p - 'A' + 10);
        else
            return fail(http, -EPROTO);
        if (++digits > 16 || size > (~uint64_t(0) - digit) / 16) return fail(http, -EPROTO);
        size = size * 16 + digit;
        ++p;
    }
    if (!digits || !valid_value(p)) return fail(http, -EPROTO);
    if (size > ~uint64_t(0) - http->info.body_bytes || (http->limit && size > http->limit - http->info.body_bytes))
        return fail(http, -EMSGSIZE);
    http->chunk_remaining = size;
    http->chunk_phase     = size ? 1 : 3;
    return 1;
}

int prepare_body(Http *http)
{
    if (http->info.state != XHTTP_READING_BODY) return 1;
    if (http->chunked)
    {
        for (;;)
        {
            if (http->chunk_phase == 0)
            {
                int result = chunk_size(http);
                if (result <= 0) return result;
            }
            if (http->chunk_phase == 1) break;
            if (http->chunk_phase == 2)
            {
                int result = line(http);
                if (result <= 0) return result;
                if (http->line_bytes) return fail(http, -EPROTO);
                http->line_bytes  = 0;
                http->chunk_phase = 0;
                continue;
            }
            if (http->chunk_phase == 3)
            {
                int result = line(http);
                if (result <= 0) return result;
                uint32_t length  = http->line_bytes;
                http->line_bytes = 0;
                if (length + 2 > XHTTP_MAX_HEADER_BYTES - http->trailer_bytes) return fail(http, -EMSGSIZE);
                http->trailer_bytes += length + 2;
                if (!length)
                {
                    complete(http);
                    return 1;
                }
                if (++http->trailer_count > XHTTP_MAX_HEADERS) return fail(http, -EMSGSIZE);
                char *name, *value;
                if (split_header(http->line, &name, &value) || framing_header(name)) return fail(http, -EPROTO);
                continue;
            }
        }
    }
    int result = fill(http);
    if (result == 2)
    {
        if (http->chunked || http->length_seen) return fail(http, -EPROTO);
        complete(http);
        return 1;
    }
    if (result > 0 && http->limit && http->info.body_bytes >= http->limit) return fail(http, -EMSGSIZE);
    return result;
}

uint32_t events(const Http *http)
{
    if (http->info.state == XHTTP_FAILED) return XHTTP_ERROR;
    uint32_t value = http->headers_ready ? XHTTP_HEADERS_READY : 0;
    if (http->info.state == XHTTP_COMPLETE) value |= XHTTP_DONE;
    if (http->info.state == XHTTP_READING_BODY && http->begin < http->end &&
        (!http->chunked || (http->chunk_phase == 1 && http->chunk_remaining)))
        value |= XHTTP_BODY_READY;
    return value;
}
} // namespace
} // namespace xnet_sdk

using namespace xnet_sdk;

extern "C" XNetResult xapi_HttpRequestStart(const XHttpRequestDesc *desc, XHttpHandle *out)
{
    if (!same_process()) return -ECHILD;
    if (!desc || !out || desc->size != sizeof(*desc) || desc->version != 1 ||
        (desc->flags & ~XHTTP_REUSE_CONNECTION) || desc->reserved2[0] ||
        desc->reserved2[1] || desc->total_timeout_ms < -1 || desc->max_redirects > 10 ||
        desc->header_count > XHTTP_MAX_HEADERS || (desc->header_count && !desc->headers) ||
        desc->body_bytes > XHTTP_MAX_REQUEST_BYTES || (desc->body_bytes && !desc->body) ||
        desc->ca_bytes > XTLS_MAX_CA_BYTES || (desc->ca_data && desc->ca_file) ||
        ((!desc->ca_data) != (!desc->ca_bytes)))
        return -EINVAL;
    int error = token_cancelled(desc->cancel);
    if (error) return error;
    auto *http = static_cast<Http *>(calloc(1, sizeof(Http)));
    if (!http) return -ENOMEM;
    http->info.size       = sizeof(http->info);
    http->info.version    = 1;
    http->info.state      = XHTTP_CONNECTING;
    http->external_cancel = desc->cancel;
    http->limit           = desc->max_response_bytes;
    http->redirect_limit  = desc->max_redirects;
    uint64_t now          = now_ms();
    if (now == ~uint64_t(0))
    {
        destroy(http);
        return -EIO;
    }
    http->deadline =
        desc->total_timeout_ms < 0 ? 0 : now + uint32_t(desc->total_timeout_ms ? desc->total_timeout_ms : 30000);
    error              = parse_url(desc->url, &http->url);
    const char *method = desc->method ? desc->method : "GET";
    uint32_t    length = 0;
    while (!error && method[length])
    {
        if (length == sizeof(http->method) - 1 || !token_character(unsigned(method[length])))
            error = -EINVAL;
        else
            ++length;
    }
    if (!error && !length) error = -EINVAL;
    if (!error && !strcmp(method, "CONNECT")) error = -EOPNOTSUPP;
    if (!error) memcpy(http->method, method, length);
    http->reuse = (desc->flags & XHTTP_REUSE_CONNECTION) &&
                  (!strcmp(http->method, "GET") || !strcmp(http->method, "HEAD")) && !desc->body_bytes;
    uint32_t header_bytes = 0;
    for (uint32_t i = 0; !error && i < desc->header_count; ++i)
    {
        OwnedHeader &header = http->request_headers[i];
        error               = copy_string(desc->headers[i].name, 256, &header.name);
        if (!error) error = copy_string(desc->headers[i].value, 8193, &header.value);
        if (error) break;
        if (!header.name[0] || framing_header(header.name) || !valid_value(header.value)) error = -EINVAL;
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(header.name); !error && *p; ++p)
            if (!token_character(*p)) error = -EINVAL;
        header_bytes += uint32_t(strlen(header.name) + strlen(header.value) + 4);
        if (header_bytes > XHTTP_MAX_HEADER_BYTES) error = -EMSGSIZE;
    }
    if (!error && desc->body_bytes)
    {
        http->body = static_cast<unsigned char *>(malloc(desc->body_bytes));
        if (!http->body)
            error = -ENOMEM;
        else
        {
            memcpy(http->body, desc->body, desc->body_bytes);
            http->body_size = desc->body_bytes;
        }
    }
    if (!error && desc->ca_data)
    {
        http->ca_data = static_cast<unsigned char *>(malloc(desc->ca_bytes + 1));
        if (!http->ca_data)
            error = -ENOMEM;
        else
        {
            http->ca_bytes = uint32_t(desc->ca_bytes);
            memcpy(http->ca_data, desc->ca_data, http->ca_bytes);
            http->ca_data[http->ca_bytes] = 0;
        }
    }
    if (!error && desc->ca_file) error = copy_string(desc->ca_file, 4096, &http->ca_file);
    if (!error) error = serialize(http->url, http->info.final_url);
    if (!error) error = build_request(http);
    if (!error) error = publish(http, kind_http, destroy, out);
    if (error) destroy(http);
    return error;
}

extern "C" XNetResult xapi_HttpWait(XHttpHandle handle, uint32_t mask, const XNetIoOptions *io, uint32_t *out)
{
    if (!out || !mask || (mask & ~XHTTP_EVENTS_ALL)) return -EINVAL;
    *out = 0;
    Reference ref(handle, kind_http);
    if (ref.error) return ref.error;
    Budget budget(io);
    if (budget.error) return budget.error;
    auto *http = static_cast<Http *>(ref.object);
    for (;;)
    {
        if (int error = cancelled(http, &budget)) return fail(http, error);
        int progress = step(http);
        if (http->info.state == XHTTP_READING_BODY) progress = prepare_body(http);
        *out = events(http) & (mask | XHTTP_ERROR | XHTTP_DONE);
        if (*out) return 1;
        // Streaming completion needs the application to consume body bytes.
        // Waiting only for DONE must never spin while that consumer is blocked.
        if (http->info.state == XHTTP_READING_BODY && http->begin < http->end) return 0;
        if (!budget.io.timeout_ms || budget.expired()) return 0;
        if (progress > 0 && http->info.state != XHTTP_READING_BODY) continue;
        int result = await(http, budget);
        if (result < 0)
        {
            if (result == -ETIMEDOUT && (!http->deadline || now_ms() < http->deadline)) return 0;
            fail(http, result);
            if (result == int(XNET_CANCELLED)) return result;
        }
    }
}

extern "C" XNetResult xapi_HttpGetResponseInfo(XHttpHandle handle, XHttpResponseInfo *info)
{
    if (!info || info->size != sizeof(*info) || info->version != 1) return -EINVAL;
    Reference ref(handle, kind_http);
    if (ref.error) return ref.error;
    auto *http   = static_cast<Http *>(ref.object);
    *info        = http->info;
    info->events = events(http);
    if (__atomic_load_n(&http->cancelled, __ATOMIC_ACQUIRE))
    {
        info->state  = XHTTP_FAILED;
        info->result = XNET_CANCELLED;
        info->events = XHTTP_ERROR;
    }
    return 0;
}

extern "C" XNetResult xapi_HttpGetTlsInfo(XHttpHandle handle, XTlsPeerInfo *info)
{
    if (!info || info->size != sizeof(*info) || info->version != 1) return -EINVAL;
    Reference ref(handle, kind_http);
    if (ref.error) return ref.error;
    auto *http = static_cast<Http *>(ref.object);
    if (!http->url.secure) return -EOPNOTSUPP;
    if (!http->tls && !http->tls_info.size) return -EINPROGRESS;
    if (http->tls) xapi_TlsGetPeerInfo(http->tls, &http->tls_info);
    *info         = http->tls_info;
    info->size    = sizeof(*info);
    info->version = 1;
    return 0;
}

extern "C" XNetResult xapi_HttpGetHeader(XHttpHandle handle, const char *name, uint32_t index, char *value,
                                         uint32_t capacity, uint32_t *required)
{
    if (!name || !required || (capacity && !value)) return -EINVAL;
    Reference ref(handle, kind_http);
    if (ref.error) return ref.error;
    auto *http = static_cast<Http *>(ref.object);
    if (!http->headers_ready) return http->info.state == XHTTP_FAILED ? http->info.result : -EINPROGRESS;
    const char *field = header_value(http, name, index);
    if (!field) return -ENOENT;
    *required = uint32_t(strlen(field) + 1);
    if (!capacity) return 0;
    if (capacity < *required) return -ENOBUFS;
    memcpy(value, field, *required);
    return 0;
}

extern "C" XNetResult xapi_HttpRead(XHttpHandle handle, void *data, uint64_t capacity, const XNetIoOptions *io)
{
    if ((capacity && !data) || capacity > XNET_MAX_TRANSFER_BYTES) return -EINVAL;
    Reference ref(handle, kind_http);
    if (ref.error) return ref.error;
    Budget budget(io);
    if (budget.error) return budget.error;
    auto *http = static_cast<Http *>(ref.object);
    if (!capacity) return 0;
    uint64_t copied = 0;
    while (copied < capacity)
    {
        int error = check(http, &budget);
        if (error) return copied ? XNetResult(copied) : error;
        if (budget.expired()) return copied ? XNetResult(copied) : -ETIMEDOUT;
        if (http->info.state == XHTTP_COMPLETE) return copied;
        int progress;
        if (http->info.state != XHTTP_READING_BODY)
            progress = step(http);
        else
            progress = prepare_body(http);
        if (progress < 0) return copied ? XNetResult(copied) : progress;
        if (http->info.state == XHTTP_COMPLETE) return copied;
        if (http->info.state == XHTTP_READING_BODY && http->begin < http->end &&
            (!http->chunked || http->chunk_phase == 1))
        {
            uint64_t amount = http->end - http->begin;
            if (amount > capacity - copied) amount = capacity - copied;
            if (http->length_seen && amount > http->content_remaining) amount = http->content_remaining;
            if (http->chunked && amount > http->chunk_remaining) amount = http->chunk_remaining;
            if (http->limit && amount > http->limit - http->info.body_bytes)
                amount = http->limit - http->info.body_bytes;
            if (!amount) return copied ? XNetResult(copied) : fail(http, -EMSGSIZE);
            if (amount > ~uint64_t(0) - http->info.body_bytes)
                return copied ? XNetResult(copied) : fail(http, -EMSGSIZE);
            memcpy(static_cast<unsigned char *>(data) + copied, http->input + http->begin, amount);
            copied                += amount;
            http->begin           += uint32_t(amount);
            http->info.body_bytes += amount;
            if (http->length_seen)
            {
                http->content_remaining -= amount;
                if (!http->content_remaining) complete(http);
            }
            if (http->chunked)
            {
                http->chunk_remaining -= amount;
                if (!http->chunk_remaining) http->chunk_phase = 2;
            }
            continue;
        }
        if (progress > 0) continue;
        if (copied) return copied;
        if (!budget.io.timeout_ms) return -EAGAIN;
        if (budget.expired()) return -ETIMEDOUT;
        int result = await(http, budget);
        if (result < 0)
        {
            if (result == -ETIMEDOUT && (!http->deadline || now_ms() < http->deadline)) return result;
            return fail(http, result);
        }
    }
    return copied;
}

extern "C" XNetResult xapi_HttpCancel(XHttpHandle handle)
{
    return xnet_sdk::cancel(handle, kind_http);
}
extern "C" XNetResult xapi_HttpClose(XHttpHandle handle)
{
    return xnet_sdk::close(handle, kind_http);
}

extern "C" XNetResult xapi_HttpCloseIdleConnections(void)
{
    if (!same_process()) return -ECHILD;
    pool_clear();
    return 0;
}
