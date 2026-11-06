// SPDX-License-Identifier: Apache-2.0
#include "internal.h"

namespace xnet_sdk {
namespace {
Object  *objects;
int      registry_lock;
uint64_t process_owner, next_id;

void lock()
{
    while (__atomic_exchange_n(&registry_lock, 1, __ATOMIC_ACQUIRE))
        raw_call(SYS_SCHED_YIELD);
}

void unlock()
{
    __atomic_store_n(&registry_lock, 0, __ATOMIC_RELEASE);
}
} // namespace

int64_t raw_call(uint64_t number, uint64_t a, uint64_t b, uint64_t c)
{
    return (int64_t)enter_syscall(number, a, b, c, 0, 0, 0);
}

bool same_process()
{
    uint64_t pid      = (uint64_t)raw_call(SYS_GETPID);
    uint64_t expected = 0;
    __atomic_compare_exchange_n(&process_owner, &expected, pid, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return __atomic_load_n(&process_owner, __ATOMIC_ACQUIRE) == pid;
}

uint64_t now_ms()
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) < 0 || time.tv_sec < 0 || time.tv_nsec < 0) return ~uint64_t(0);
    return uint64_t(time.tv_sec) * 1000 + uint64_t(time.tv_nsec) / 1000000;
}

char ascii_lower(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? char(ch + ('a' - 'A')) : ch;
}

bool equal_ascii(const char *a, const char *b)
{
    while (*a && *b && ascii_lower(*a) == ascii_lower(*b))
    {
        ++a;
        ++b;
    }
    return *a == *b;
}

int copy_string(const char *source, uint32_t maximum, char **out)
{
    *out = NULL;
    if (!source) return -EINVAL;
    uint32_t length = 0;
    while (length < maximum && source[length])
        ++length;
    if (length == maximum) return -ENAMETOOLONG;
    char *copy = static_cast<char *>(malloc(length + 1));
    if (!copy) return -ENOMEM;
    memcpy(copy, source, length + 1);
    *out = copy;
    return 0;
}

int token_cancelled(XNetHandle token)
{
    if (!token) return 0;
    XNetIoOptions io = {sizeof(io), XNET_ABI_VERSION, 0, 0, token};
    return int(xapi_NetWaitMany(NULL, 0, &io));
}

int publish(Object *object, uint32_t kind, void (*destroy)(Object *), uint64_t *out)
{
    if (!out) return -EINVAL;
    int error = int(xapi_NetCreateCancel(&object->cancel));
    if (error) return error;
    object->kind    = kind;
    object->refs    = 1;
    object->destroy = destroy;
    lock();
    if (next_id == 0x0000ffffffffffffULL)
        error = -ENFILE;
    else
    {
        object->id   = 0x5841000000000000ULL | ++next_id;
        object->next = objects;
        objects      = object;
    }
    unlock();
    if (error)
    {
        xapi_NetClose(object->cancel);
        object->cancel = 0;
    }
    else
        *out = object->id;
    return error;
}

void release(Object *object)
{
    if (!object) return;
    lock();
    bool last = --object->refs == 0;
    unlock();
    if (last) object->destroy(object);
}

Reference::Reference(uint64_t id, uint32_t kind, bool acquire_busy) : object(NULL), error(0), busy(false)
{
    // A fork can copy a held registry/crypto lock. Reject inherited SDK state
    // before touching the allocator or any copied lock; exec resets the runtime.
    if (!same_process())
    {
        error = -ECHILD;
        return;
    }
    lock();
    for (Object *candidate = objects; candidate; candidate = candidate->next)
        if (candidate->id == id && candidate->kind == kind)
        {
            ++candidate->refs;
            object = candidate;
            break;
        }
    unlock();
    if (!object)
    {
        error = -EBADF;
        return;
    }
    if (acquire_busy)
    {
        if (__atomic_exchange_n(&object->busy, 1, __ATOMIC_ACQUIRE))
            error = -EBUSY;
        else
            busy = true;
    }
    if (!error && __atomic_load_n(&object->closed, __ATOMIC_ACQUIRE)) error = -EBADF;
}

Reference::~Reference()
{
    if (busy) __atomic_store_n(&object->busy, 0, __ATOMIC_RELEASE);
    release(object);
}

int close(uint64_t id, uint32_t kind)
{
    if (!same_process()) return -ECHILD;
    Object *object = NULL;
    lock();
    for (Object **link = &objects; *link; link = &(*link)->next)
        if ((*link)->id == id && (*link)->kind == kind)
        {
            object = *link;
            *link  = object->next;
            __atomic_store_n(&object->closed, 1, __ATOMIC_RELEASE);
            break;
        }
    unlock();
    if (!object) return -EBADF;
    // The detached table reference pins the cancellation handle through signal.
    xapi_NetSignalCancel(object->cancel);
    release(object);
    return 0;
}

int cancel(uint64_t id, uint32_t kind)
{
    Reference ref(id, kind, false);
    if (ref.error) return ref.error;
    __atomic_store_n(&ref.object->cancelled, 1, __ATOMIC_RELEASE);
    xapi_NetSignalCancel(ref.object->cancel);
    return 0;
}

Budget::Budget(const XNetIoOptions *options) : start(now_ms()), error(0)
{
    io = {sizeof(io), XNET_ABI_VERSION, 0, 0, 0};
    if (options) io = *options;
    if (io.size != sizeof(io) || io.version != XNET_ABI_VERSION || io.reserved || io.timeout_ms < -1) error = -EINVAL;
    if (!error && start == ~uint64_t(0)) error = -EIO;
    if (!error && io.cancel) error = token_cancelled(io.cancel);
}

int32_t Budget::remaining() const
{
    if (io.timeout_ms <= 0) return io.timeout_ms;
    uint64_t elapsed = now_ms() - start;
    return elapsed >= uint32_t(io.timeout_ms) ? 0 : int32_t(uint32_t(io.timeout_ms) - elapsed);
}

bool Budget::expired() const
{
    return io.timeout_ms > 0 && remaining() == 0;
}

int cancelled(Object *object, const Budget *budget)
{
    if (__atomic_load_n(&object->closed, __ATOMIC_ACQUIRE) || __atomic_load_n(&object->cancelled, __ATOMIC_ACQUIRE))
        return int(XNET_CANCELLED);
    int result = token_cancelled(object->external_cancel);
    if (result) return result == -EBADF ? int(XNET_CANCELLED) : result;
    if (budget && budget->io.cancel && budget->io.cancel != object->external_cancel)
    {
        result = token_cancelled(budget->io.cancel);
        if (result) return result == -EBADF ? int(XNET_CANCELLED) : result;
    }
    return 0;
}

int wait_native(Object *object, XNetHandle transport, uint32_t mask, Budget &budget, uint64_t deadline, uint32_t *ready)
{
    if (int error = cancelled(object, &budget)) return error;
    if (budget.expired()) return -ETIMEDOUT;
    uint64_t now = now_ms();
    if (deadline && now >= deadline) return -ETIMEDOUT;
    int32_t timeout = budget.remaining();
    if (timeout < 0 || timeout > 50) timeout = 50;
    if (!transport && timeout > 10) timeout = 10;
    if (deadline && uint64_t(timeout) > deadline - now) timeout = int32_t(deadline - now);
    XNetWaitItem items[3] = {};
    unsigned     count    = 0;
    if (transport) items[count++] = {sizeof(XNetWaitItem), 1, transport, mask, 0, 0};
    unsigned cancel_index = count;
    items[count++]        = {sizeof(XNetWaitItem), 1, object->cancel, XNET_DONE, 0, 0};
    if (object->external_cancel) items[count++] = {sizeof(XNetWaitItem), 1, object->external_cancel, XNET_DONE, 0, 0};
    XNetIoOptions options = {sizeof(options), 1, timeout, 0, budget.io.cancel};
    int           result  = int(xapi_NetWaitMany(items, count, &options));
    if (result < 0) return result;
    for (unsigned i = cancel_index; i < count; ++i)
        if (items[i].ready) return int(XNET_CANCELLED);
    if (transport && items[0].ready)
    {
        if (ready) *ready = items[0].ready;
        if (items[0].ready & XNET_INVALID) return -EBADF;
        return 1;
    }
    return 0;
}

void dial_close(Dial &dial)
{
    if (dial.query) xapi_NetClose(dial.query);
    if (dial.socket) xapi_NetClose(dial.socket);
    dial.query = dial.socket = 0;
}

// Keep one alternate-family attempt inside the original overall deadline.
// Literal addresses never fall back; certificate identity remains the hostname.
static int dial_retry(Dial &dial, int error, uint64_t deadline)
{
    if (!dial.hostname || dial.resolve_family || (deadline && now_ms() >= deadline)) return error;
    switch (error)
    {
    case -EAFNOSUPPORT:
    case -ENETDOWN:
    case -ENETUNREACH:
    case -EHOSTUNREACH:
    case -ECONNREFUSED:
    case -ETIMEDOUT:
    case -ECONNRESET:
    case -ECONNABORTED:
    case -EADDRNOTAVAIL: break;
    default: return error;
    }
    uint32_t family = dial.peer.address.family == XNET_IPV6 ? XNET_IPV4 : XNET_IPV6;
    dial_close(dial);
    dial.peer             = {};
    dial.resolve_family   = family;
    dial.connect_deadline = 0;
    dial.phase            = 0;
    return 1;
}

int dial_step(Object *owner, Dial &dial, const char *host, uint16_t port, uint64_t deadline)
{
    if (int error = cancelled(owner)) return error;
    if (deadline && now_ms() >= deadline) return -ETIMEDOUT;
    switch (dial.phase)
    {
    case 0:
        if (!dial.hostname && xapi_NetParseAddress(host, &dial.peer.address) == 0)
        {
            dial.peer.port = port;
            dial.phase     = 2;
            return 1;
        }
        else
        {
            dial.hostname = true;
            uint64_t now = now_ms();
            if (deadline && now >= deadline) return -ETIMEDOUT;
            uint64_t remaining = deadline ? deadline - now : 5000;
            if (remaining > 0x7fffffffU) remaining = 0x7fffffffU;
            XNetResolveDesc desc = {
                sizeof(desc),  1,     host, dial.resolve_family, 0, port, 0, uint32_t(remaining ? remaining : 1),
                owner->cancel, {0, 0}
            };
            int result = int(xapi_NetResolveStart(&desc, &dial.query));
            if (result) return result;
            dial.phase = 1;
            return 1;
        }
    case 1: {
        uint32_t count  = 0;
        int      result = int(xapi_NetResolveGetResult(dial.query, &dial.peer, 1, &count));
        if (result == -EINPROGRESS) return 0;
        if (result) return result;
        if (!count) return -ENOENT;
        xapi_NetClose(dial.query);
        dial.query = 0;
        dial.phase = 2;
        return 1;
    }
    case 2: {
        XNetOpenDesc desc = {
            sizeof(desc), 1, dial.peer.address.family, XNET_TCP, 0, 0, {0, 0}
        };
        int result = int(xapi_NetOpen(&desc, &dial.socket));
        if (result == -EAGAIN) return 0;
        if (result) return dial_retry(dial, result, deadline);
        dial.phase = 3;
        return 1;
    }
    case 3: {
        if (dial.hostname && !dial.resolve_family)
        {
            uint64_t now = now_ms();
            if (deadline && now >= deadline) return -ETIMEDOUT;
            uint64_t slice = deadline ? (deadline - now) / 2 : 3000;
            if (slice > 3000) slice = 3000;
            dial.connect_deadline = now + (slice ? slice : 1);
        }
        XNetIoOptions io     = {sizeof(io), 1, 0, 0, owner->cancel};
        int           result = int(xapi_NetConnect(dial.socket, &dial.peer, &io));
        if (result && result != -EINPROGRESS) return dial_retry(dial, result, deadline);
        dial.phase = result ? 4 : 5;
        return 1;
    }
    case 4: {
        XNetSocketInfo info = {};
        info.size           = sizeof(info);
        info.version        = 1;
        int result          = int(xapi_NetQuery(dial.socket, &info));
        if (result) return dial_retry(dial, result, deadline);
        if (info.state == XNET_CONNECTING)
        {
            if (dial.connect_deadline && now_ms() >= dial.connect_deadline)
                return dial_retry(dial, -ETIMEDOUT, deadline);
            return 0;
        }
        if (info.connection_result) return dial_retry(dial, int(info.connection_result), deadline);
        if (info.state != XNET_CONNECTED) return dial_retry(dial, -ECONNABORTED, deadline);
        dial.phase = 5;
        return 1;
    }
    default: return 1;
    }
}

XNetHandle dial_interest(const Dial &dial, uint32_t *mask)
{
    if (dial.phase == 1)
    {
        *mask = XNET_DONE;
        return dial.query;
    }
    *mask = XNET_CONNECTION_READY | XNET_ERROR;
    return dial.phase == 4 ? dial.socket : 0;
}

int load_ca_file(const char *path, unsigned char **data, uint32_t *bytes)
{
    *data      = NULL;
    *bytes     = 0;
    int64_t fd = raw_call(SYS_OPEN, (uint64_t)path, 0, 0);
    if (fd < 0) return int(fd);
    uint32_t capacity = 16384, used = 0;
    auto    *buffer = static_cast<unsigned char *>(malloc(capacity + 1));
    int      error  = buffer ? 0 : -ENOMEM;
    while (!error)
    {
        if (used == capacity)
        {
            if (capacity == XTLS_MAX_CA_BYTES)
            {
                unsigned char extra;
                int64_t       n = raw_call(SYS_READ, fd, (uint64_t)&extra, 1);
                error           = n == 0 ? 0 : n < 0 ? int(n) : -EFBIG;
                break;
            }
            uint32_t next = capacity * 2;
            if (next > XTLS_MAX_CA_BYTES) next = XTLS_MAX_CA_BYTES;
            auto *grown = static_cast<unsigned char *>(realloc(buffer, next + 1));
            if (!grown)
            {
                error = -ENOMEM;
                break;
            }
            buffer   = grown;
            capacity = next;
        }
        int64_t n = raw_call(SYS_READ, fd, (uint64_t)(buffer + used), capacity - used);
        if (n == -EINTR) continue;
        if (n < 0)
        {
            error = int(n);
            break;
        }
        if (!n) break;
        used += uint32_t(n);
    }
    raw_call(SYS_CLOSE, fd);
    if (!error && !used) error = -EBADMSG;
    if (error)
    {
        free(buffer);
        return error;
    }
    buffer[used] = 0;
    *data        = buffer;
    *bytes       = used;
    return 0;
}
} // namespace xnet_sdk
