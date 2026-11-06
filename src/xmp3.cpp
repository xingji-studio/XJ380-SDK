#include "include/xmp3.h"
#include "include/libsys.h"
#include "include/stdlib.h"
#include "include/string.h"
#include "include/unistd.h"
#include "include/errno.h"
#include "include/xtuiapi.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#define DR_MP3_ONLY_MP3
#define DRMP3_API static
#include "../../third_party/dr_mp3/dr_mp3.h"

namespace
{
struct Decoder
{
    drmp3 mp3;
    int fd, error;
    bool eof, initialized;
    uint64_t length, offset, position, total, heap_bytes, read_budget;
    const unsigned char *memory;
    void *owned;
};
struct Slot
{
    uint64_t id;
    Decoder *decoder;
    bool busy;
};
static Slot slots[32];
static int table_busy;
static uint64_t owner, next_id;

static long long call(uint64_t op, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0)
{
    return (long long)enter_syscall(op, a, b, c, 0, 0, 0);
}

static bool own_process()
{
    uint64_t pid = call(SYS_GETPID), expected = 0;
    __atomic_compare_exchange_n(&owner, &expected, pid, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return __atomic_load_n(&owner, __ATOMIC_ACQUIRE) == pid;
}

static void lock()
{
    while (__atomic_exchange_n(&table_busy, 1, __ATOMIC_ACQUIRE)) usleep(1000);
}

static void unlock()
{
    __atomic_store_n(&table_busy, 0, __ATOMIC_RELEASE);
}

static Slot *take(uint64_t id, int *error)
{
    *error = -EBADF;
    if (!own_process()) return NULL;
    lock();
    for (Slot &s : slots)
    {
        if (!id || s.id != id) continue;
        if (s.busy) { *error = -EBUSY; break; }
        s.busy = true;
        unlock();
        return &s;
    }
    unlock();
    return NULL;
}

static void put(Slot *s)
{
    lock();
    s->busy = false;
    unlock();
}

static size_t read(void *context, void *out, size_t bytes)
{
    Decoder *d = (Decoder *)context;
    uint64_t left = d->length - d->offset;
    if (left && !d->read_budget) { d->error = -E2BIG; return 0; }
    if (bytes > left) bytes = left;
    if (bytes > d->read_budget) { d->error = -E2BIG; return 0; }
    d->read_budget -= bytes;
    if (d->memory)
    {
        memcpy(out, d->memory + d->offset, bytes);
        d->offset += bytes;
        return bytes;
    }
    size_t done = 0;
    while (done < bytes)
    {
        long long n = call(SYS_READ, d->fd, (uint64_t)((char *)out + done), bytes - done);
        if (n == -EINTR) continue;
        if (n <= 0) { d->error = n < 0 ? (int)n : -EIO; break; }
        done += n;
    }
    d->offset += done;
    return done;
}

static drmp3_bool32 seek(void *context, int offset, drmp3_seek_origin origin)
{
    Decoder *d = (Decoder *)context;
    uint64_t base = origin == drmp3_seek_origin_start ? 0 : d->offset;
    if (offset < 0 || base > d->length || (uint64_t)offset > d->length - base) return false;
    uint64_t target = base + offset;
    if (!d->memory)
    {
        long long result = call(SYS_LSEEK, d->fd, target, 0);
        if (result < 0) { d->error = (int)result; return false; }
    }
    d->offset = target;
    return true;
}

static void destroy(Decoder *d)
{
    if (d->initialized) drmp3_uninit(&d->mp3);
    if (d->fd >= 0) call(SYS_CLOSE, d->fd);
    free(d->owned);
    free(d);
}

// Preserve alignment and cap backend-controlled allocations at 1 MiB/instance.
struct Allocation { size_t bytes, reserved; };

static void *allocate(size_t bytes, void *context)
{
    Decoder *d = (Decoder *)context;
    if (bytes > 1024 * 1024 - d->heap_bytes) { d->error = -ENOMEM; return NULL; }
    Allocation *a = (Allocation *)malloc(sizeof(Allocation) + bytes);
    if (!a) { d->error = -ENOMEM; return NULL; }
    a->bytes = bytes;
    d->heap_bytes += bytes;
    return a + 1;
}

static void deallocate(void *data, void *context)
{
    if (!data) return;
    Decoder *d = (Decoder *)context;
    Allocation *a = (Allocation *)data - 1;
    d->heap_bytes -= a->bytes;
    free(a);
}

static void *resize(void *data, size_t bytes, void *context)
{
    if (!data) return allocate(bytes, context);
    Allocation *old = (Allocation *)data - 1;
    void *next = allocate(bytes, context);
    if (!next) return NULL;
    memcpy(next, data, old->bytes < bytes ? old->bytes : bytes);
    deallocate(data, context);
    return next;
}

static XMp3Result publish(Decoder *d, XMp3Handle *handle)
{
    d->read_budget = 4 * 1024 * 1024;
    drmp3_allocation_callbacks allocator = {d, allocate, resize, deallocate};
    if (!drmp3_init(&d->mp3, read, seek, d, &allocator))
    {
        int error = d->error ? d->error : -ENOEXEC;
        destroy(d);
        return error;
    }
    d->initialized = true;
    if (d->error || !d->mp3.channels || d->mp3.channels > 2)
    {
        int error = d->error ? d->error : -EOPNOTSUPP;
        destroy(d);
        return error;
    }
    d->total = XMP3_FRAMES_UNKNOWN;
    lock();
    for (Slot &s : slots)
    {
        if (s.id) continue;
        s = {++next_id, d, false};
        *handle = s.id;
        unlock();
        return 0;
    }
    unlock();
    destroy(d);
    return -ENOSPC;
}
}

extern "C" XMp3Result xapi_Mp3DecoderOpenFile(const char *path, XMp3Handle *handle)
{
    if (!path || !handle) return -EINVAL;
    if (!own_process()) return -EOPNOTSUPP;
    INT64 access = xapi_RequestFileAccess(path, XFA_READ);
    if (access != 0) return access > 0 ? -EACCES : access;
    long long fd = call(SYS_OPEN, (uint64_t)path, 02000000);
    if (fd < 0) return fd;
    long long length = call(SYS_LSEEK, fd, 0, 2);
    if (length < 0) { call(SYS_CLOSE, fd); return length; }
    if (call(SYS_LSEEK, fd, 0, 0) < 0) { call(SYS_CLOSE, fd); return -EIO; }
    Decoder *d = (Decoder *)calloc(1, sizeof(Decoder));
    if (!d) { call(SYS_CLOSE, fd); return -ENOMEM; }
    d->fd = fd;
    d->length = length;
    return publish(d, handle);
}

extern "C" XMp3Result xapi_Mp3DecoderOpenMemory(const void *data, XAudioU64 bytes, XAudioU32 flags,
                                               XMp3Handle *handle)
{
    if (!data || !bytes || !handle || flags & ~XMP3_MEMORY_BORROW) return -EINVAL;
    if (bytes > XMP3_MAX_MEMORY_BYTES) return -EFBIG;
    if (!own_process()) return -EOPNOTSUPP;
    Decoder *d = (Decoder *)calloc(1, sizeof(Decoder));
    if (!d) return -ENOMEM;
    d->fd = -1;
    d->length = bytes;
    d->memory = (const unsigned char *)data;
    if (!(flags & XMP3_MEMORY_BORROW))
    {
        d->owned = malloc(bytes);
        if (!d->owned) { destroy(d); return -ENOMEM; }
        memcpy(d->owned, data, bytes);
        d->memory = (const unsigned char *)d->owned;
    }
    return publish(d, handle);
}

extern "C" XMp3Result xapi_Mp3DecoderGetInfo(XMp3Handle handle, XMp3Info *info)
{
    if (!info || info->size != sizeof(*info) || info->version != XMP3_ABI_VERSION) return -EINVAL;
    int error;
    Slot *s = take(handle, &error);
    if (!s) return error;
    Decoder *d = s->decoder;
    *info = {sizeof(*info), XMP3_ABI_VERSION, d->mp3.sampleRate, d->mp3.channels, XAUDIO_S16_LE,
             (XAudioU32)(XMP3_SEEKABLE | (d->eof ? XMP3_EOF : 0)), d->position, d->total, d->error};
    put(s);
    return 0;
}

extern "C" XMp3Result xapi_Mp3DecoderReadPCM(XMp3Handle handle, short *pcm, XAudioU32 frames)
{
    if (frames && !pcm) return -EINVAL;
    int error;
    Slot *s = take(handle, &error);
    if (!s) return error;
    Decoder *d = s->decoder;
    // Bounded calls preserve responsive cancellation and protect size arithmetic.
    d->read_budget = 4 * 1024 * 1024;
    uint32_t capacity = frames > 4096 ? 4096 : frames;
    uint64_t n = d->error || !capacity ? 0 : drmp3_read_pcm_frames_s16(&d->mp3, capacity, pcm);
    d->position += n;
    if (capacity && !n && !d->error) { d->eof = true; d->total = d->position; }
    XMp3Result result = n ? (XMp3Result)n : d->error;
    put(s);
    return result;
}

extern "C" XMp3Result xapi_Mp3DecoderSeek(XMp3Handle handle, XAudioU64 frame, XAudioU64 *actual)
{
    if (!actual) return -EINVAL;
    int error;
    Slot *s = take(handle, &error);
    if (!s) return error;
    Decoder *d = s->decoder;
    if (d->total != XMP3_FRAMES_UNKNOWN && frame > d->total) error = -EINVAL;
    else
    {
        d->error = 0;
        d->read_budget = d->length; // Explicit synchronous Seek may scan the whole file.
        bool ok = drmp3_seek_to_pcm_frame(&d->mp3, frame);
        error = ok && !d->error ? 0 : d->error ? d->error : -EIO;
        d->error = error;
        if (!error) { d->position = frame; d->eof = false; *actual = frame; }
    }
    put(s);
    return error;
}

extern "C" XMp3Result xapi_Mp3DecoderClose(XMp3Handle handle)
{
    int error;
    Slot *s = take(handle, &error);
    if (!s) return error;
    Decoder *d = s->decoder;
    lock();
    *s = {};
    unlock();
    destroy(d);
    return 0;
}
