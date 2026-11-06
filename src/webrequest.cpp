#include "./include/webrequest.h"

#include "../include/httpclient.h"
#include "../include/https_client.h"

int xtls_request(const xhttp_request_t *request,
                 xhttp_response_callback_t callback,
                 void *user_data) __attribute__((weak));

typedef struct xapi_webrequest_buffer
{
    char   *data;
    size_t  size;
    size_t  capacity;
    int     oom;
} xapi_webrequest_buffer_t;

static char *xapi_webrequest_dup_range(const char *start, size_t len)
{
    if (start == NULL) {
        return NULL;
    }

    char *out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }

    if (len > 0) {
        memcpy(out, start, len);
    }
    out[len] = '\0';
    return out;
}

static char *xapi_webrequest_dup_string(const char *text)
{
    if (text == NULL) {
        return NULL;
    }

    return xapi_webrequest_dup_range(text, strlen(text));
}

static int xapi_webrequest_append(const char *data, int len, void *user_data)
{
    xapi_webrequest_buffer_t *buffer = (xapi_webrequest_buffer_t *)user_data;
    if (buffer == NULL || data == NULL || len <= 0) {
        return 0;
    }

    if (buffer->size + (size_t)len + 1 > buffer->capacity) {
        size_t new_capacity = buffer->capacity == 0 ? 1024 : buffer->capacity;
        while (new_capacity < buffer->size + (size_t)len + 1) {
            new_capacity *= 2;
        }

        char *new_data = (char *)realloc(buffer->data, new_capacity);
        if (new_data == NULL) {
            buffer->oom = 1;
            return 1;
        }

        buffer->data     = new_data;
        buffer->capacity = new_capacity;
    }

    memcpy(buffer->data + buffer->size, data, (size_t)len);
    buffer->size += (size_t)len;
    buffer->data[buffer->size] = '\0';
    return 0;
}

static const char *xapi_webrequest_find_body(const char *response)
{
    if (response == NULL) {
        return NULL;
    }

    const char *body = strstr(response, "\r\n\r\n");
    if (body != NULL) {
        return body + 4;
    }

    body = strstr(response, "\n\n");
    if (body != NULL) {
        return body + 2;
    }

    return response;
}

static int xapi_webrequest_is_scheme_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '+' || ch == '-' || ch == '.';
}

static int xapi_webrequest_parse_port(const char *text, size_t len, uint16_t *out_port)
{
    if (text == NULL || len == 0 || out_port == NULL) {
        return 0;
    }

    unsigned int value = 0;
    for (size_t i = 0; i < len; ++i) {
        char ch = text[i];
        if (ch < '0' || ch > '9') {
            return 0;
        }
        value = value * 10u + (unsigned int)(ch - '0');
        if (value > 65535u) {
            return 0;
        }
    }

    if (value == 0u) {
        return 0;
    }

    *out_port = (uint16_t)value;
    return 1;
}

static const char *xapi_webrequest_skip_scheme(const char *url)
{
    if (url == NULL) {
        return NULL;
    }

    for (const char *p = url; *p != '\0'; ++p) {
        if (*p == ':') {
            if (p[1] == '/' && p[2] == '/') {
                return p + 3;
            }
            break;
        }
        if (!xapi_webrequest_is_scheme_char(*p)) {
            break;
        }
    }

    return url;
}

extern "C" char *xapi_httpGet(const char *url, uint16_t port, bool tls)
{
    if (url == NULL || url[0] == '\0') {
        return NULL;
    }

    const char *resolved_url = xapi_webrequest_skip_scheme(url);
    if (resolved_url == NULL || resolved_url[0] == '\0') {
        return NULL;
    }

    const char *host_begin = resolved_url;
    const char *path_begin = strchr(resolved_url, '/');
    const char *host_end   = path_begin != NULL ? path_begin : (resolved_url + strlen(resolved_url));

    if (host_end == host_begin) {
        return NULL;
    }

    uint16_t resolved_port = port;

    if (host_begin[0] == '[') {
        const char *rb = strchr(host_begin, ']');
        if (rb == NULL || rb >= host_end) {
            return NULL;
        }
        if (rb + 1 < host_end && rb[1] == ':') {
            uint16_t parsed_port = 0;
            if (!xapi_webrequest_parse_port(rb + 2, (size_t)(host_end - (rb + 2)), &parsed_port)) {
                return NULL;
            }
            resolved_port = parsed_port;
            host_end      = rb + 1;
        }
    } else {
        const char *colon = NULL;
        for (const char *p = host_begin; p < host_end; ++p) {
            if (*p == ':') {
                colon = p;
                break;
            }
        }
        if (colon != NULL) {
            uint16_t parsed_port = 0;
            if (!xapi_webrequest_parse_port(colon + 1, (size_t)(host_end - (colon + 1)), &parsed_port)) {
                return NULL;
            }
            resolved_port = parsed_port;
            host_end      = colon;
        }
    }

    if (resolved_port == 0) {
        resolved_port = tls ? 443 : 80;
    }

    char *host = xapi_webrequest_dup_range(host_begin, (size_t)(host_end - host_begin));
    if (host == NULL) {
        return NULL;
    }

    char *path = path_begin != NULL ? xapi_webrequest_dup_string(path_begin) : xapi_webrequest_dup_string("/");
    if (path == NULL) {
        free(host);
        return NULL;
    }

    xhttp_request_t request;
    request.method        = "GET";
    request.host          = host;
    request.port          = resolved_port;
    request.path          = path;
    request.body          = NULL;
    request.content_type  = NULL;
    request.extra_headers = "Connection: close\r\n";

    xapi_webrequest_buffer_t response;
    memset(&response, 0, sizeof(response));

    int status = XHTTP_ERR_BAD_ARGUMENT;
    if (tls) {
        if (xtls_request == NULL) {
            free(path);
            free(host);
            return NULL;
        }
        status = xtls_request(&request, xapi_webrequest_append, &response);
    } else {
        status = xhttp_request(&request, xapi_webrequest_append, &response);
    }

    free(path);
    free(host);

    if (status != XHTTP_OK || response.oom || response.data == NULL || response.size == 0) {
        free(response.data);
        return NULL;
    }

    const char *body = xapi_webrequest_find_body(response.data);
    char       *out  = xapi_webrequest_dup_string(body);
    free(response.data);
    return out;
}
