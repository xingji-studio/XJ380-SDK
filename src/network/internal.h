// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../include/errno.h"
#include "../include/libsys.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/time.h"
#include "../include/xhttp.h"
#include "../include/xnet_dns.h"
#include "../include/xnet_runtime.h"

namespace xnet_sdk {
constexpr uint32_t kind_tls = 1, kind_http = 2;
constexpr uint32_t tls_record_bytes = 16384;
struct Object
{
    Object    *next;
    uint64_t   id;
    uint32_t   kind, refs;
    int        busy, closed, cancelled;
    XNetHandle cancel, external_cancel;
    void (*destroy)(Object *);
};
bool     same_process();
uint64_t now_ms();
int64_t  raw_call(uint64_t number, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0);
int      copy_string(const char *source, uint32_t maximum, char **out);
bool     equal_ascii(const char *a, const char *b);
char     ascii_lower(char ch);
int      token_cancelled(XNetHandle token);
int      publish(Object *object, uint32_t kind, void (*destroy)(Object *), uint64_t *out);
void     release(Object *object);
int      close(uint64_t id, uint32_t kind);
int      cancel(uint64_t id, uint32_t kind);

struct Reference
{
    Object *object;
    int     error;
    bool    busy;
    Reference(uint64_t id, uint32_t kind, bool lock = true);
    ~Reference();
    Reference(const Reference &)            = delete;
    Reference &operator=(const Reference &) = delete;
};
struct Budget
{
    XNetIoOptions io;
    uint64_t      start;
    int           error;
    explicit Budget(const XNetIoOptions *options);
    int32_t remaining() const;
    bool    expired() const;
};
int cancelled(Object *object, const Budget *budget = NULL);
// 1 transport ready, 0 timer/slice, or negative error. Internal and descriptor
// cancellation tokens participate in every wait, including a closed token.
int wait_native(Object *object, XNetHandle transport, uint32_t mask, Budget &budget, uint64_t deadline,
                uint32_t *ready = NULL);

struct Dial
{
    XNetHandle   query, socket;
    XNetEndpoint peer;
    uint32_t     phase, resolve_family;
    uint64_t     connect_deadline;
    bool         hostname;
};
int        dial_step(Object *owner, Dial &dial, const char *host, uint16_t port, uint64_t deadline);
void       dial_close(Dial &dial);
XNetHandle dial_interest(const Dial &dial, uint32_t *mask);
int        load_ca_file(const char *path, unsigned char **data, uint32_t *bytes);
// HTTP opt-in transports keep trust bytes and have no request-owned cancel token.
int        tls_http_enable_reuse(XTlsHandle tls);
bool       tls_http_reuse_trust(XTlsHandle tls, const void *ca_data, uint32_t ca_bytes, const char *ca_file);
} // namespace xnet_sdk
