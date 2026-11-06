// SPDX-License-Identifier: Apache-2.0
#include "internal.h"
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

namespace xnet_sdk {
namespace {
struct Tls : Object
{
    Dial                     dial;
    XTlsPeerInfo             info;
    uint64_t                 deadline;
    char                    *host, *name, *ca_file, *trust_path;
    unsigned char           *ca_data, *pending;
    uint32_t                 ca_bytes, pending_bytes, handshake_want, send_want, receive_want;
    int                      transport_error, entropy_error, clock_error;
    bool                     initialized, eof, shutdown_started, write_shutdown, http_reuse, trust_inline;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       config;
    mbedtls_ctr_drbg_context random;
    mbedtls_x509_crt         roots;
};

void destroy(Object *base)
{
    auto *tls = static_cast<Tls *>(base);
    dial_close(tls->dial);
    if (tls->initialized)
    {
        mbedtls_ssl_free(&tls->ssl);
        mbedtls_ssl_config_free(&tls->config);
        mbedtls_ctr_drbg_free(&tls->random);
        mbedtls_x509_crt_free(&tls->roots);
    }
    if (tls->cancel) xapi_NetClose(tls->cancel);
    if (tls->pending)
    {
        volatile unsigned char *erase = tls->pending;
        for (uint32_t i = 0; i < tls->pending_bytes; ++i)
            erase[i] = 0;
    }
    free(tls->pending);
    free(tls->ca_data);
    free(tls->host);
    free(tls->name);
    free(tls->ca_file);
    free(tls->trust_path);
    free(tls);
}

int fail(Tls *tls, int error, uint32_t failure, int backend = 0)
{
    if (tls->info.state != XTLS_FAILED)
    {
        tls->info.state         = XTLS_FAILED;
        tls->info.result        = error;
        tls->info.failure       = failure;
        tls->info.backend_error = backend;
        dial_close(tls->dial);
    }
    return int(tls->info.result);
}

bool valid_host(const char *host)
{
    if (!host[0]) return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(host); *p; ++p)
        if (*p <= 32 || *p >= 127 || *p == '/' || *p == '\\' || *p == '@' || *p == '[' || *p == ']') return false;
    return true;
}

uint32_t native_verify_flags(uint32_t flags)
{
    uint32_t result = 0;
    if (flags & MBEDTLS_X509_BADCERT_EXPIRED) result |= XTLS_CERT_EXPIRED;
    if (flags & MBEDTLS_X509_BADCERT_FUTURE) result |= XTLS_CERT_FUTURE;
    if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) result |= XTLS_CERT_NAME;
    if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) result |= XTLS_CERT_UNTRUSTED;
    if (flags & ~(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE | MBEDTLS_X509_BADCERT_CN_MISMATCH |
                  MBEDTLS_X509_BADCERT_NOT_TRUSTED))
        result |= XTLS_CERT_OTHER;
    return result;
}

int entropy(void *context, unsigned char *data, size_t bytes)
{
    auto *tls = static_cast<Tls *>(context);
    if (bytes > 0xffffffffULL) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    tls->entropy_error = xapi_secure_random(data, uint32_t(bytes));
    return tls->entropy_error ? MBEDTLS_ERR_ENTROPY_SOURCE_FAILED : 0;
}

int bio_send(void *context, const unsigned char *data, size_t bytes)
{
    auto *tls = static_cast<Tls *>(context);
    if (cancelled(tls))
    {
        tls->transport_error = int(XNET_CANCELLED);
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    XNetResult result = xapi_NetSend(tls->dial.socket, data, bytes, NULL);
    if (result == -EAGAIN || (!result && bytes)) return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (result < 0)
    {
        tls->transport_error = int(result);
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return int(result);
}

int bio_receive(void *context, unsigned char *data, size_t bytes)
{
    auto *tls = static_cast<Tls *>(context);
    if (cancelled(tls))
    {
        tls->transport_error = int(XNET_CANCELLED);
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    XNetResult result = xapi_NetReceive(tls->dial.socket, data, bytes, NULL);
    if (result == -EAGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
    if (result < 0)
    {
        tls->transport_error = int(result);
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return int(result);
}

int load_trust(Tls *tls, unsigned char **data, uint32_t *bytes, const char **selected)
{
    static const char *const paths[] = {"/etc/ssl/certs/ca-certificates.crt", "/etc/ssl/cert.pem",
                                        "/system/ssl/certs/ca-certificates.crt", "/system/ssl/cert.pem"};
    if (tls->ca_file)
    {
        *selected = tls->ca_file;
        return load_ca_file(tls->ca_file, data, bytes);
    }
    int error = -ENOENT;
    for (const char *path : paths)
    {
        *selected = path;
        error = load_ca_file(path, data, bytes);
        // A denial or invalid bundle must not fall through to a different CA.
        if (!error || (error != -ENOENT && error != -ENOTDIR)) break;
    }
    return error;
}

int prepare(Tls *tls)
{
    if (!tls->ca_data)
    {
        const char *selected = NULL;
        int error = load_trust(tls, &tls->ca_data, &tls->ca_bytes, &selected);
        if (error) return fail(tls, error, XTLS_FAILURE_TRUST);
        if (tls->http_reuse && (error = copy_string(selected, 4096, &tls->trust_path)))
            return fail(tls, error, XTLS_FAILURE_TRUST);
    }
    int error = xapi_tls_runtime_init();
    if (error) return fail(tls, error, XTLS_FAILURE_ENTROPY);
    struct timespec time;
    if (clock_gettime(CLOCK_REALTIME, &time) < 0 || time.tv_sec < 1577836800LL)
        return fail(tls, -ERANGE, XTLS_FAILURE_CLOCK);
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->config);
    mbedtls_ctr_drbg_init(&tls->random);
    mbedtls_x509_crt_init(&tls->roots);
    tls->initialized = true;
    // PEM requires the terminating NUL; DER requires the exact binary length.
    bool              pem      = false;
    static const char marker[] = "-----BEGIN CERTIFICATE-----";
    for (uint32_t i = 0; i + sizeof(marker) - 1 <= tls->ca_bytes; ++i)
        if (!memcmp(tls->ca_data + i, marker, sizeof(marker) - 1))
        {
            pem = true;
            break;
        }
    error = mbedtls_x509_crt_parse(&tls->roots, tls->ca_data, tls->ca_bytes + (pem ? 1 : 0));
    if (error) return fail(tls, -EBADMSG, XTLS_FAILURE_TRUST, error);
    if (!tls->http_reuse)
    {
        free(tls->ca_data);
        tls->ca_data = NULL;
    }
    static const unsigned char personal[] = "XJ380-XTls-v1";
    error = mbedtls_ctr_drbg_seed(&tls->random, entropy, tls, personal, sizeof(personal) - 1);
    if (error) return fail(tls, tls->entropy_error ? tls->entropy_error : -EIO, XTLS_FAILURE_ENTROPY, error);
    error = mbedtls_ssl_config_defaults(&tls->config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);
    if (error) return fail(tls, -EPROTO, XTLS_FAILURE_PROTOCOL, error);
    mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&tls->config, &tls->roots, NULL);
    mbedtls_ssl_conf_verify(&tls->config, xapi_tls_verify_time, &tls->clock_error);
    mbedtls_ssl_conf_rng(&tls->config, mbedtls_ctr_drbg_random, &tls->random);
    mbedtls_ssl_conf_min_tls_version(&tls->config, MBEDTLS_SSL_VERSION_TLS1_2);
    error = mbedtls_ssl_setup(&tls->ssl, &tls->config);
    if (!error) error = mbedtls_ssl_set_hostname(&tls->ssl, tls->name);
    if (error) return fail(tls, -EPROTO, XTLS_FAILURE_PROTOCOL, error);
    mbedtls_ssl_set_bio(&tls->ssl, tls, bio_send, bio_receive, NULL);
    tls->info.state = XTLS_RESOLVING;
    return 1;
}

int backend_failure(Tls *tls, int backend)
{
    uint32_t flags = mbedtls_ssl_get_verify_result(&tls->ssl);
    // No negotiated session is reported as all bits set, not as an actual
    // certificate failure (for example, a peer closes during ClientHello).
    tls->info.verify_flags = flags == 0xffffffffU ? 0 : native_verify_flags(flags);
    if (tls->clock_error) return fail(tls, -ERANGE, XTLS_FAILURE_CLOCK, backend);
    if (tls->entropy_error) return fail(tls, tls->entropy_error, XTLS_FAILURE_ENTROPY, backend);
    if (tls->info.verify_flags || backend == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
        return fail(tls, -EACCES, XTLS_FAILURE_VERIFY, backend);
    if (tls->transport_error) return fail(tls, tls->transport_error, XTLS_FAILURE_TRANSPORT, backend);
    return fail(tls, -EPROTO, XTLS_FAILURE_PROTOCOL, backend);
}

uint32_t wanted(int result)
{
    return result == MBEDTLS_ERR_SSL_WANT_WRITE ? XNET_WRITABLE : XNET_READABLE;
}

bool retry(int result)
{
    return result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE;
}

int step(Tls *tls)
{
    if (tls->info.state == XTLS_FAILED) return int(tls->info.result);
    if (int error = cancelled(tls)) return fail(tls, error, XTLS_FAILURE_TRANSPORT);
    if (tls->info.state == XTLS_READY) return 1;
    if (now_ms() >= tls->deadline) return fail(tls, -ETIMEDOUT, XTLS_FAILURE_TRANSPORT);
    if (tls->info.state == XTLS_PREPARING) return prepare(tls);
    if (tls->dial.phase != 5)
    {
        int result      = dial_step(tls, tls->dial, tls->host, tls->info.peer.port, tls->deadline);
        tls->info.state = tls->dial.phase < 2 ? XTLS_RESOLVING : XTLS_CONNECTING;
        if (result < 0) return fail(tls, result, XTLS_FAILURE_TRANSPORT);
        if (tls->dial.phase != 5) return result;
        tls->info.peer  = tls->dial.peer;
        tls->info.state = XTLS_HANDSHAKING;
    }
    int result = mbedtls_ssl_handshake(&tls->ssl);
    if (retry(result))
    {
        tls->handshake_want = wanted(result);
        return 0;
    }
    if (result) return backend_failure(tls, result);
    tls->info.verify_flags = native_verify_flags(mbedtls_ssl_get_verify_result(&tls->ssl));
    if (tls->info.verify_flags || !mbedtls_ssl_get_peer_cert(&tls->ssl)) return fail(tls, -EACCES, XTLS_FAILURE_VERIFY);
    tls->info.state      = XTLS_READY;
    tls->info.result     = 0;
    tls->deadline        = 0;
    const char *protocol = mbedtls_ssl_get_version(&tls->ssl);
    const char *cipher   = mbedtls_ssl_get_ciphersuite(&tls->ssl);
    if (protocol) strncpy(tls->info.protocol, protocol, sizeof(tls->info.protocol) - 1);
    if (cipher) strncpy(tls->info.cipher, cipher, sizeof(tls->info.cipher) - 1);
    return 1;
}

XNetHandle interest(Tls *tls, uint32_t *mask)
{
    if (tls->dial.phase != 5) return dial_interest(tls->dial, mask);
    *mask = tls->handshake_want ? tls->handshake_want : XNET_READABLE;
    return tls->dial.socket;
}

int ensure_ready(Tls *tls, Budget &budget)
{
    for (;;)
    {
        if (int error = cancelled(tls, &budget)) return fail(tls, error, XTLS_FAILURE_TRANSPORT);
        int progress = step(tls);
        if (progress < 0) return progress;
        if (tls->info.state == XTLS_READY) return 0;
        if (budget.expired()) return -ETIMEDOUT;
        if (progress) continue;
        if (!budget.io.timeout_ms) return -EAGAIN;
        uint32_t   mask;
        XNetHandle handle = interest(tls, &mask);
        int        result = wait_native(tls, handle, mask, budget, tls->deadline);
        if (result < 0)
        {
            if (result == int(XNET_CANCELLED)) return fail(tls, result, XTLS_FAILURE_TRANSPORT);
            if (tls->deadline && now_ms() >= tls->deadline) return fail(tls, -ETIMEDOUT, XTLS_FAILURE_TRANSPORT);
            return result;
        }
    }
}
} // namespace

int tls_http_enable_reuse(XTlsHandle handle)
{
    Reference ref(handle, kind_tls);
    if (ref.error) return ref.error;
    auto *tls = static_cast<Tls *>(ref.object);
    if (tls->info.state != XTLS_PREPARING || tls->external_cancel) return -EINVAL;
    tls->http_reuse = true;
    tls->trust_inline = tls->ca_data != NULL;
    return 0;
}

bool tls_http_reuse_trust(XTlsHandle handle, const void *ca_data, uint32_t ca_bytes, const char *ca_file)
{
    Reference ref(handle, kind_tls);
    if (ref.error) return false;
    auto *tls = static_cast<Tls *>(ref.object);
    if (!tls->http_reuse || tls->info.state != XTLS_READY || tls->eof || tls->pending ||
        tls->shutdown_started || cancelled(tls)) return false;
    if (ca_data)
        return tls->trust_inline && ca_bytes == tls->ca_bytes && !memcmp(ca_data, tls->ca_data, ca_bytes);
    if (tls->trust_inline || (ca_file == NULL) != (tls->ca_file == NULL) ||
        (ca_file && strcmp(ca_file, tls->ca_file))) return false;
    // Reopen with current rights before reuse. A changed, missing or denied
    // trust file causes a pool miss; the normal connect path reports/authorizes
    // it, preserving the browser's existing trust-file permission flow.
    unsigned char *current = NULL;
    uint32_t bytes = 0;
    const char *selected = NULL;
    int error = load_trust(tls, &current, &bytes, &selected);
    bool same = !error && tls->trust_path && !strcmp(selected, tls->trust_path) && bytes == tls->ca_bytes &&
                !memcmp(current, tls->ca_data, bytes);
    free(current);
    return same;
}
} // namespace xnet_sdk

using namespace xnet_sdk;

extern "C" XNetResult xapi_TlsConnectStart(const XTlsConnectDesc *desc, XTlsHandle *out)
{
    if (!same_process()) return -ECHILD;
    if (!desc || !out || desc->size != sizeof(*desc) || desc->version != 1 || desc->reserved[0] || desc->reserved[1] ||
        !desc->port || desc->port > 65535 || desc->total_timeout_ms > 0x7fffffffU ||
        desc->ca_bytes > XTLS_MAX_CA_BYTES || (desc->ca_data && desc->ca_file) ||
        ((!desc->ca_data) != (!desc->ca_bytes)))
        return -EINVAL;
    int error = token_cancelled(desc->cancel);
    if (error) return error;
    auto *tls = static_cast<Tls *>(calloc(1, sizeof(Tls)));
    if (!tls) return -ENOMEM;
    tls->info.size       = sizeof(tls->info);
    tls->info.version    = 1;
    tls->info.state      = XTLS_PREPARING;
    tls->info.peer.port  = uint16_t(desc->port);
    tls->external_cancel = desc->cancel;
    uint64_t start       = now_ms();
    if (start == ~uint64_t(0))
    {
        destroy(tls);
        return -EIO;
    }
    tls->deadline = start + (desc->total_timeout_ms ? desc->total_timeout_ms : 30000);
    error         = copy_string(desc->host, 254, &tls->host);
    if (!error) error = copy_string(desc->server_name ? desc->server_name : desc->host, 254, &tls->name);
    if (!error && (!valid_host(tls->host) || !valid_host(tls->name))) error = -EINVAL;
    if (!error)
    {
        XNetAddress identity;
        if (!xapi_NetParseAddress(tls->name, &identity) && identity.family == XNET_IPV6)
        {
            // A scope selects the local NIC; it is not a certificate identity
            // and must never be sent to the remote endpoint in TLS SNI.
            char *zone = strchr(tls->name, '%');
            if (zone) *zone = 0;
        }
    }
    if (!error && desc->ca_file) error = copy_string(desc->ca_file, 4096, &tls->ca_file);
    if (!error && desc->ca_data)
    {
        tls->ca_data = static_cast<unsigned char *>(malloc(uint32_t(desc->ca_bytes) + 1));
        if (!tls->ca_data)
            error = -ENOMEM;
        else
        {
            tls->ca_bytes = uint32_t(desc->ca_bytes);
            memcpy(tls->ca_data, desc->ca_data, tls->ca_bytes);
            tls->ca_data[tls->ca_bytes] = 0;
        }
    }
    if (!error) strncpy(tls->info.server_name, tls->name, sizeof(tls->info.server_name) - 1);
    if (!error) error = publish(tls, kind_tls, destroy, out);
    if (error) destroy(tls);
    return error;
}

extern "C" XNetResult xapi_TlsWait(XTlsHandle handle, uint32_t mask, const XNetIoOptions *io, uint32_t *events)
{
    if (!events || !mask || (mask & ~XTLS_EVENTS_ALL)) return -EINVAL;
    *events = 0;
    Reference ref(handle, kind_tls);
    if (ref.error) return ref.error;
    Budget budget(io);
    if (budget.error) return budget.error;
    auto *tls = static_cast<Tls *>(ref.object);
    for (;;)
    {
        if (int error = cancelled(tls, &budget)) return fail(tls, error, XTLS_FAILURE_TRANSPORT);
        int progress = step(tls);
        if (tls->info.state == XTLS_FAILED)
        {
            *events = XTLS_ERROR;
            return 1;
        }
        if (tls->info.state == XTLS_READY)
        {
            uint32_t ready = XTLS_EVENT_READY;
            if (tls->eof) ready |= XTLS_EOF | XTLS_READABLE;
            if (mbedtls_ssl_get_bytes_avail(&tls->ssl)) ready |= XTLS_READABLE;
            uint32_t read_mask   = tls->receive_want ? tls->receive_want : XNET_READABLE;
            uint32_t write_mask  = tls->send_want ? tls->send_want : XNET_WRITABLE;
            uint32_t native_mask = ((mask & XTLS_READABLE) ? read_mask : 0) |
                                   ((mask & XTLS_WRITABLE) && !tls->write_shutdown ? write_mask : 0);
            if (native_mask)
            {
                XNetWaitItem item   = {sizeof(item), 1, tls->dial.socket, native_mask, 0, 0};
                int          result = int(xapi_NetWaitMany(&item, 1, NULL));
                if (result < 0) return result;
                if (item.ready & (XNET_ERROR | XNET_CLOSED | XNET_INVALID))
                {
                    fail(tls, -ECONNRESET, XTLS_FAILURE_TRANSPORT);
                    *events = XTLS_ERROR;
                    return 1;
                }
                if (item.ready & read_mask) ready |= XTLS_READABLE;
                if ((item.ready & write_mask) && !tls->write_shutdown) ready |= XTLS_WRITABLE;
            }
            *events = ready & (mask | XTLS_EOF | XTLS_ERROR);
            if (*events) return 1;
            uint32_t wait_mask = native_mask ? native_mask : XNET_READABLE;
            if (!budget.io.timeout_ms || budget.expired()) return 0;
            int result = wait_native(tls, tls->dial.socket, wait_mask, budget, 0);
            if (result == -ETIMEDOUT) return 0;
            if (result < 0) return fail(tls, result, XTLS_FAILURE_TRANSPORT);
        }
        else
        {
            if (!budget.io.timeout_ms || budget.expired()) return 0;
            if (progress > 0) continue;
            uint32_t   native_mask;
            XNetHandle transport = interest(tls, &native_mask);
            int        result    = wait_native(tls, transport, native_mask, budget, tls->deadline);
            if (result == -ETIMEDOUT)
            {
                if (now_ms() >= tls->deadline)
                {
                    fail(tls, result, XTLS_FAILURE_TRANSPORT);
                    continue;
                }
                return 0;
            }
            if (result < 0) return fail(tls, result, XTLS_FAILURE_TRANSPORT);
        }
    }
}

extern "C" XNetResult xapi_TlsGetPeerInfo(XTlsHandle handle, XTlsPeerInfo *info)
{
    if (!info || info->size != sizeof(*info) || info->version != 1) return -EINVAL;
    Reference ref(handle, kind_tls);
    if (ref.error) return ref.error;
    auto *tls            = static_cast<Tls *>(ref.object);
    *info                = tls->info;
    info->buffered_bytes = tls->initialized ? uint32_t(mbedtls_ssl_get_bytes_avail(&tls->ssl)) : 0;
    info->events = tls->info.state == XTLS_FAILED ? XTLS_ERROR : tls->info.state == XTLS_READY ? XTLS_EVENT_READY : 0;
    if (tls->eof) info->events |= XTLS_EOF;
    if (__atomic_load_n(&tls->cancelled, __ATOMIC_ACQUIRE))
    {
        info->state  = XTLS_FAILED;
        info->result = XNET_CANCELLED;
        info->events = XTLS_ERROR;
    }
    return 0;
}

extern "C" XNetResult xapi_TlsSend(XTlsHandle handle, const void *data, uint64_t bytes, const XNetIoOptions *io)
{
    if ((bytes && !data) || bytes > XNET_MAX_TRANSFER_BYTES) return -EINVAL;
    Reference ref(handle, kind_tls);
    if (ref.error) return ref.error;
    auto  *tls = static_cast<Tls *>(ref.object);
    Budget budget(io);
    if (budget.error) return budget.error;
    if (!bytes) return 0;
    int error = ensure_ready(tls, budget);
    if (error) return error;
    if (tls->shutdown_started) return -ESHUTDOWN;
    uint32_t count = bytes > tls_record_bytes ? tls_record_bytes : uint32_t(bytes);
    if (tls->pending)
    {
        if (count < tls->pending_bytes || memcmp(data, tls->pending, tls->pending_bytes)) return -EBUSY;
    }
    else
    {
        tls->pending = static_cast<unsigned char *>(malloc(count));
        if (!tls->pending) return -ENOMEM;
        tls->pending_bytes = count;
        memcpy(tls->pending, data, count);
    }
    for (;;)
    {
        if ((error = cancelled(tls, &budget))) return fail(tls, error, XTLS_FAILURE_TRANSPORT);
        int result = mbedtls_ssl_write(&tls->ssl, tls->pending, tls->pending_bytes);
        if (result > 0)
        {
            free(tls->pending);
            tls->pending       = NULL;
            tls->pending_bytes = 0;
            tls->send_want     = 0;
            return result;
        }
        if (!retry(result)) return backend_failure(tls, result);
        tls->send_want = wanted(result);
        if (!budget.io.timeout_ms) return -EAGAIN;
        if (budget.expired()) return fail(tls, -ETIMEDOUT, XTLS_FAILURE_TRANSPORT);
        result = wait_native(tls, tls->dial.socket, tls->send_want, budget, 0);
        if (result < 0) return fail(tls, result, XTLS_FAILURE_TRANSPORT);
    }
}

extern "C" XNetResult xapi_TlsReceive(XTlsHandle handle, void *data, uint64_t capacity, const XNetIoOptions *io)
{
    if ((capacity && !data) || capacity > XNET_MAX_TRANSFER_BYTES) return -EINVAL;
    Reference ref(handle, kind_tls);
    if (ref.error) return ref.error;
    auto  *tls = static_cast<Tls *>(ref.object);
    Budget budget(io);
    if (budget.error) return budget.error;
    if (!capacity) return 0;
    int error = ensure_ready(tls, budget);
    if (error) return error;
    if (tls->eof) return 0;
    for (;;)
    {
        if ((error = cancelled(tls, &budget))) return fail(tls, error, XTLS_FAILURE_TRANSPORT);
        int result = mbedtls_ssl_read(&tls->ssl, static_cast<unsigned char *>(data), capacity);
        if (result > 0)
        {
            tls->receive_want = 0;
            return result;
        }
        if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        {
            tls->eof = true;
            return 0;
        }
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        if (result == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
        if (!retry(result)) return backend_failure(tls, result); // TCP EOF without close_notify is truncation.
        tls->receive_want = wanted(result);
        if (!budget.io.timeout_ms) return -EAGAIN;
        if (budget.expired()) return -ETIMEDOUT;
        result = wait_native(tls, tls->dial.socket, tls->receive_want, budget, 0);
        if (result == -ETIMEDOUT) return result;
        if (result < 0) return fail(tls, result, XTLS_FAILURE_TRANSPORT);
    }
}

extern "C" XNetResult xapi_TlsShutdown(XTlsHandle handle, const XNetIoOptions *io)
{
    Reference ref(handle, kind_tls);
    if (ref.error) return ref.error;
    auto  *tls = static_cast<Tls *>(ref.object);
    Budget budget(io);
    if (budget.error) return budget.error;
    int error = ensure_ready(tls, budget);
    if (error) return error;
    if (tls->pending) return -EBUSY;
    if (tls->write_shutdown) return 0;
    tls->shutdown_started = true;
    for (;;)
    {
        if ((error = cancelled(tls, &budget))) return fail(tls, error, XTLS_FAILURE_TRANSPORT);
        int result = mbedtls_ssl_close_notify(&tls->ssl);
        if (!result)
        {
            tls->write_shutdown = true;
            return 0;
        }
        if (!retry(result)) return backend_failure(tls, result);
        tls->send_want = wanted(result);
        if (!budget.io.timeout_ms) return -EAGAIN;
        if (budget.expired()) return fail(tls, -ETIMEDOUT, XTLS_FAILURE_TRANSPORT);
        result = wait_native(tls, tls->dial.socket, wanted(result), budget, 0);
        if (result < 0) return fail(tls, result, XTLS_FAILURE_TRANSPORT);
    }
}

extern "C" XNetResult xapi_TlsCancel(XTlsHandle handle)
{
    return xnet_sdk::cancel(handle, kind_tls);
}
extern "C" XNetResult xapi_TlsClose(XTlsHandle handle)
{
    return xnet_sdk::close(handle, kind_tls);
}
