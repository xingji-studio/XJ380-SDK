#include "include/xaudio_player.h"
#include "include/libsys.h"
#include "include/stdlib.h"
#include "include/string.h"
#include "include/unistd.h"
#include "include/errno.h"
#include "include/xtuiapi.h"
#include "include/sys/mman.h"

#include "include/xmp3.h"

extern "C" long long xapi_audio_spawn(void *, void (*)(void *), void *, int *);

namespace
{
constexpr unsigned stack_bytes = 256 * 1024;
constexpr unsigned player_limit = 16;

struct Player
{
    XAudioPlayer id;
    XAudioHandle stream;
    uint64_t owner, data_offset, data_bytes, cursor, base_frame, played_base, duration;
    uint32_t rate, channels, bits, state, volume, frames, offset;
    int fd, busy, quit, child_tid, error;
    void *stack;
    bool mp3, decoder_ready;
    XMp3Handle decoder;
    unsigned rate_factor;
    int16_t pcm[512 * 2];
};
static Player *players[player_limit];
static uint64_t next_id;
static int api_busy;
static uint64_t api_owner;

static void acquire(int *word)
{
    while (__atomic_exchange_n(word, 1, __ATOMIC_ACQUIRE)) usleep(1000);
}

static void release(int *word)
{
    __atomic_store_n(word, 0, __ATOMIC_RELEASE);
}

static long long file_call(uint64_t op, uint64_t a, uint64_t b = 0, uint64_t c = 0)
{
    return (long long)enter_syscall(op, a, b, c, 0, 0, 0);
}

// A fork may snapshot api_busy while another thread owns it. Check process
// identity before touching any inherited lock or allocator state. The child
// can use the player library again after exec initializes a fresh runtime.
static bool same_process()
{
    uint64_t current = (uint64_t)file_call(SYS_GETPID, 0);
    uint64_t expected = 0;
    __atomic_compare_exchange_n(&api_owner, &expected, current, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return __atomic_load_n(&api_owner, __ATOMIC_ACQUIRE) == current;
}

static bool read_exact(Player *p, void *data, uint64_t size)
{
    unsigned char *out = (unsigned char *)data;
    while (size)
    {
        long long n = file_call(SYS_READ, p->fd, (uint64_t)out, size);
        if (n == -EINTR) continue;
        if (n <= 0)
        {
            p->error = n < 0 ? (int)n : -EIO;
            return false;
        }
        out += n;
        size -= n;
    }
    return true;
}

static uint32_t le32(const unsigned char *v)
{
    return (uint32_t)v[0] | (uint32_t)v[1] << 8 | (uint32_t)v[2] << 16 | (uint32_t)v[3] << 24;
}

static uint32_t le16(const unsigned char *v)
{
    return (uint32_t)v[0] | (uint32_t)v[1] << 8;
}

static bool inspect(Player *p, const char *path)
{
    long long length = file_call(SYS_LSEEK, p->fd, 0, 2);
    if (length < 12 || file_call(SYS_LSEEK, p->fd, 0, 0) < 0) return false;
    unsigned char header[16];
    if (!read_exact(p, header, 12)) return false;
    if (memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4))
    {
        file_call(SYS_CLOSE, p->fd);
        p->fd = -1;
        p->mp3 = true;
        p->error = (int)xapi_Mp3DecoderOpenFile(path, &p->decoder);
        if (p->error) return false;
        p->decoder_ready = true;
        XMp3Info info = {};
        info.size = sizeof(info);
        info.version = XMP3_ABI_VERSION;
        p->error = (int)xapi_Mp3DecoderGetInfo(p->decoder, &info);
        p->channels = info.channels;
        p->rate_factor = info.sample_rate == 12000 ? 2 : 1;
        p->rate = info.sample_rate * p->rate_factor;
        p->duration = XAUDIO_DURATION_UNKNOWN;
        return !p->error;
    }
    uint64_t end = (uint64_t)le32(header + 4) + 8;
    if (end > (uint64_t)length || end < 12) return false;
    bool format = false, data = false;
    uint64_t at = 12;
    for (unsigned chunks = 0; chunks < 4096 && at + 8 <= end; ++chunks)
    {
        if (file_call(SYS_LSEEK, p->fd, at, 0) < 0 || !read_exact(p, header, 8)) return false;
        uint64_t size = le32(header + 4), start = at + 8;
        if (size > end - start) return false;
        if (!memcmp(header, "fmt ", 4))
        {
            if (size < 16 || !read_exact(p, header, 16) || le16(header) != 1) return false;
            p->channels = le16(header + 2);
            p->rate = le32(header + 4);
            p->bits = le16(header + 14);
            if (!p->channels || p->channels > 2 ||
                (p->bits != 8 && p->bits != 16 && p->bits != 24 && p->bits != 32) ||
                le16(header + 12) != p->channels * (p->bits / 8)) return false;
            format = true;
        }
        else if (!memcmp(header, "data", 4))
        {
            p->data_offset = start;
            p->data_bytes = size;
            data = true;
        }
        at = start + size;
        // Common WAV writers omit the final odd-byte pad; there is no next
        // chunk to align in that case. Interior chunks still require padding.
        if ((at & 1) && at < end) ++at;
        if (at > end) return false;
    }
    if (!format || !data || !p->rate || p->data_bytes % (p->channels * (p->bits / 8))) return false;
    uint64_t frames = p->data_bytes / (p->channels * (p->bits / 8));
    p->duration = frames * 1000000000ULL / p->rate;
    return file_call(SYS_LSEEK, p->fd, p->data_offset, 0) >= 0;
}

static unsigned decode(Player *p)
{
    if (p->mp3)
    {
        XMp3Result n = xapi_Mp3DecoderReadPCM(p->decoder, p->pcm, 512 / p->rate_factor);
        if (n < 0) { p->error = (int)n; return 0; }
        if (p->rate_factor == 2)
        {
            // 12 kHz MPEG-2.5 is adapted to XAudio's supported 24 kHz rate.
            for (unsigned i = (unsigned)n; i > 0; --i)
                for (unsigned c = 0; c < p->channels; ++c)
                {
                    int16_t value = p->pcm[(i - 1) * p->channels + c];
                    p->pcm[(2 * i - 2) * p->channels + c] = value;
                    p->pcm[(2 * i - 1) * p->channels + c] = value;
                }
        }
        return (unsigned)n * p->rate_factor;
    }
    unsigned char bytes[512 * 8];
    unsigned stride = p->channels * (p->bits / 8);
    uint64_t available = (p->data_bytes - p->cursor) / stride;
    unsigned frames = available > 512 ? 512 : (unsigned)available;
    if (frames && !read_exact(p, bytes, frames * stride)) return 0;
    p->cursor += frames * stride;
    for (unsigned i = 0; i < frames * p->channels; ++i)
    {
        const unsigned char *v = bytes + i * (p->bits / 8);
        if (p->bits == 8) p->pcm[i] = ((int)v[0] - 128) * 256;
        else if (p->bits == 16) p->pcm[i] = (int16_t)le16(v);
        else if (p->bits == 24) p->pcm[i] = (int16_t)((uint32_t)v[1] | (uint32_t)v[2] << 8);
        else p->pcm[i] = (int16_t)(le32(v) >> 16);
    }
    return frames;
}

static XAudioStatus stream_status(Player *p)
{
    XAudioStatus status = {};
    status.size = sizeof(status);
    status.version = XAUDIO_ABI_VERSION;
    XAudioResult result = xapi_AudioQuery(p->stream, &status);
    if (result < 0) p->error = (int)result;
    else if (status.state == XAUDIO_ERROR) p->error = -EIO;
    return status;
}

static int seek(Player *p, uint64_t ns)
{
    if (p->duration != XAUDIO_DURATION_UNKNOWN && ns > p->duration) return -EINVAL;
    // A shared hardware period cannot be removed for one stream. Drain the old
    // in-flight tail before establishing the new media-position baseline.
    int result = (int)xapi_AudioControl(p->stream, XAUDIO_STOP);
    if (!result) result = (int)xapi_AudioControl(p->stream, XAUDIO_DRAIN);
    uint32_t event;
    if (!result) result = (int)xapi_AudioWait(p->stream, XAUDIO_DRAINED, 2000, &event);
    if (result < 0) return result;
    XAudioStatus status = stream_status(p);
    if (p->error) return p->error;
    uint64_t frame = (ns / 1000000000ULL) * p->rate + (ns % 1000000000ULL) * p->rate / 1000000000ULL;
    bool ok;
    if (p->mp3)
    {
        XAudioU64 actual;
        ok = xapi_Mp3DecoderSeek(p->decoder, frame / p->rate_factor, &actual) == 0;
        if (ok) frame = actual * p->rate_factor;
    }
    else
    {
        uint64_t offset = frame * p->channels * (p->bits / 8);
        ok = offset <= p->data_bytes && file_call(SYS_LSEEK, p->fd, p->data_offset + offset, 0) >= 0;
        if (ok) p->cursor = offset;
    }
    if (!ok) { p->error = -EIO; p->state = XAUDIO_PLAYER_ERROR; return -EIO; }
    p->base_frame = frame;
    p->played_base = status.played_frames;
    p->frames = p->offset = 0;
    return 0;
}

static void worker(void *context)
{
    Player *p = (Player *)context;
    while (!__atomic_load_n(&p->quit, __ATOMIC_ACQUIRE))
    {
        acquire(&p->busy);
        if (p->state == XAUDIO_PLAYER_PLAYING)
        {
            // AudioWrite accepts at most 256 frames. Fill the available queue
            // before yielding; one partial write per scheduler wake cannot
            // sustain 44.1/48 kHz when wakeups are coarser than a few ms.
            for (unsigned packet = 0; packet < 32 && !p->error; ++packet)
            {
                if (p->offset == p->frames)
                {
                    p->frames = decode(p);
                    p->offset = 0;
                    if (!p->frames && !p->error)
                    {
                        p->error = (int)xapi_AudioControl(p->stream, XAUDIO_DRAIN);
                        p->state = XAUDIO_PLAYER_DRAINING;
                        break;
                    }
                }
                if (p->offset < p->frames && !p->error)
                {
                    XAudioResult n = xapi_AudioWrite(p->stream, p->pcm + p->offset * p->channels,
                                                   p->frames - p->offset, 0);
                    if (n > 0) p->offset += (unsigned)n;
                    else
                    {
                        if (n != -EAGAIN) p->error = n < 0 ? (int)n : -EIO;
                        break;
                    }
                }
            }
        }
        else if (p->state == XAUDIO_PLAYER_DRAINING)
        {
            XAudioStatus status = stream_status(p);
            if (status.events & XAUDIO_DRAINED) p->state = XAUDIO_PLAYER_COMPLETED;
        }
        if (p->error) p->state = XAUDIO_PLAYER_ERROR;
        release(&p->busy);
        usleep(1000);
    }
}

static Player *lookup(XAudioPlayer id)
{
    uint64_t owner = (uint64_t)file_call(SYS_GETPID, 0);
    for (Player *p : players)
        if (p && p->id == id && p->owner == owner) return p;
    return NULL;
}

static void destroy(Player *p)
{
    if (p->stream) xapi_AudioClose(p->stream);
    if (p->decoder_ready) xapi_Mp3DecoderClose(p->decoder);
    if (p->fd >= 0) file_call(SYS_CLOSE, p->fd);
    if (p->stack && p->stack != MAP_FAILED) munmap(p->stack, stack_bytes);
    free(p);
}
}

extern "C" XAudioResult xapi_AudioPlayerOpen(const char *path, XAudioPlayer *id)
{
    if (!path || !id) return -EINVAL;
    if (!same_process()) return -EOPNOTSUPP;
    INT64 access = xapi_RequestFileAccess(path, XFA_READ);
    if (access != 0) return access > 0 ? -EACCES : access;
    Player *p = (Player *)calloc(1, sizeof(Player));
    if (!p) return -ENOMEM;
    p->fd = -1;
    long long fd = file_call(SYS_OPEN, (uint64_t)path, 02000000); // O_RDONLY | O_CLOEXEC
    if (fd < 0) { free(p); return fd; }
    p->fd = (int)fd;
    if (!inspect(p, path))
    {
        int result = p->error ? p->error : -ENOEXEC;
        destroy(p);
        return result;
    }
    XAudioDesc desc = {};
    desc.size = sizeof(desc);
    desc.version = XAUDIO_ABI_VERSION;
    desc.direction = XAUDIO_PLAYBACK;
    desc.format = XAUDIO_S16_LE;
    desc.channels = p->channels;
    desc.sample_rate = p->rate;
    desc.buffer_frames = XAUDIO_MAX_BUFFER_FRAMES;
    XAudioResult result = xapi_AudioOpen(&desc, &p->stream);
    if (result < 0) { destroy(p); return result; }
    p->stack = mmap(NULL, stack_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p->stack == MAP_FAILED) { destroy(p); return -ENOMEM; }
    p->owner = (uint64_t)file_call(SYS_GETPID, 0);
    p->state = XAUDIO_PLAYER_READY;
    p->volume = XAUDIO_VOLUME_ONE;
    p->child_tid = 1;
    acquire(&api_busy);
    for (Player *&slot : players)
    {
        if (slot) continue;
        p->id = ++next_id;
        result = xapi_audio_spawn((char *)p->stack + stack_bytes, worker, p, &p->child_tid);
        if (result < 0) break;
        slot = p;
        *id = p->id;
        release(&api_busy);
        return 0;
    }
    release(&api_busy);
    destroy(p);
    return result < 0 ? result : -ENOSPC;
}

static XAudioResult command(XAudioPlayer id, unsigned command, uint64_t value = 0, XAudioPlayerStatus *out = NULL)
{
    if (!same_process()) return -EBADF;
    acquire(&api_busy);
    Player *p = lookup(id);
    if (!p) { release(&api_busy); return -EBADF; }
    acquire(&p->busy);
    int result = 0;
    if (command == 0)
    {
        if (p->state == XAUDIO_PLAYER_COMPLETED) result = seek(p, 0);
        if (!result) result = (int)xapi_AudioControl(p->stream, XAUDIO_RESUME);
        if (!result) p->state = XAUDIO_PLAYER_PLAYING;
    }
    else if (command == 1)
    {
        if (p->state != XAUDIO_PLAYER_PLAYING) result = -EBUSY;
        else result = (int)xapi_AudioControl(p->stream, XAUDIO_PAUSE);
        if (!result) p->state = XAUDIO_PLAYER_PAUSED;
    }
    else if (command == 2 || command == 3)
    {
        unsigned previous = p->state;
        if (value / 1000000000ULL > (~0ULL / p->rate)) result = -EINVAL;
        else result = seek(p, value);
        if (!result)
        {
            p->state = command == 2 ? XAUDIO_PLAYER_READY : previous;
            if (p->state == XAUDIO_PLAYER_PLAYING || p->state == XAUDIO_PLAYER_DRAINING)
            {
                result = (int)xapi_AudioControl(p->stream, XAUDIO_START);
                p->state = XAUDIO_PLAYER_PLAYING;
            }
            else if (command == 3) p->state = XAUDIO_PLAYER_PAUSED;
        }
    }
    else if (command == 4)
    {
        if (!out || out->size != sizeof(*out) || out->version != XAUDIO_ABI_VERSION) result = -EINVAL;
        else
        {
            XAudioStatus status = stream_status(p);
            if (p->error) p->state = XAUDIO_PLAYER_ERROR;
            uint64_t frame = p->base_frame + (status.size ? status.played_frames - p->played_base : 0);
            *out = {sizeof(*out), XAUDIO_ABI_VERSION, p->state, p->rate, p->channels, p->volume,
                    p->error, (frame / p->rate) * 1000000000ULL + (frame % p->rate) * 1000000000ULL / p->rate,
                    p->duration};
        }
    }
    else if (command == 5)
    {
        result = (int)xapi_AudioSetVolume(p->stream, (uint32_t)value);
        if (!result) p->volume = (uint32_t)value;
    }
    release(&p->busy);
    release(&api_busy);
    return result;
}

extern "C" XAudioResult xapi_AudioPlayerPlay(XAudioPlayer id)
{
    return command(id, 0);
}
extern "C" XAudioResult xapi_AudioPlayerPause(XAudioPlayer id)
{
    return command(id, 1);
}
extern "C" XAudioResult xapi_AudioPlayerStop(XAudioPlayer id)
{
    return command(id, 2);
}
extern "C" XAudioResult xapi_AudioPlayerSeek(XAudioPlayer id, XAudioU64 ns)
{
    return command(id, 3, ns);
}
extern "C" XAudioResult xapi_AudioPlayerQuery(XAudioPlayer id, XAudioPlayerStatus *out)
{
    return command(id, 4, 0, out);
}
extern "C" XAudioResult xapi_AudioPlayerSetVolume(XAudioPlayer id, XAudioU32 value)
{
    return command(id, 5, value);
}

extern "C" XAudioResult xapi_AudioPlayerClose(XAudioPlayer id)
{
    if (!same_process()) return -EBADF;
    acquire(&api_busy);
    Player *p = lookup(id);
    if (!p) { release(&api_busy); return -EBADF; }
    __atomic_store_n(&p->quit, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&p->child_tid, __ATOMIC_ACQUIRE)) usleep(1000);
    for (Player *&slot : players) if (slot == p) slot = NULL;
    destroy(p);
    release(&api_busy);
    return 0;
}
