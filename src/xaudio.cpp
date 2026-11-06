#include "include/xaudio.h"
#include "include/libsys.h"
#include "include/xtuiapi.h"
#include "include/errno.h"

static XAudioResult call(XAudioU64 op, XAudioU64 a = 0, XAudioU64 b = 0, XAudioU64 c = 0, XAudioU64 d = 0)
{
    return (XAudioResult)enter_syscall(XAPI_AUDIO, op, a, b, c, d, 0);
}

extern "C" XAudioResult xapi_AudioEnumDevices(XAudioU64 *ids, XAudioU32 capacity)
{
    return call(XAUDIO_OP_ENUM, (XAudioU64)ids, capacity);
}

extern "C" XAudioResult xapi_AudioGetDeviceInfo(XAudioU64 device, XAudioDeviceInfo *info)
{
    return call(XAUDIO_OP_DEVICE, device, (XAudioU64)info);
}

extern "C" XAudioResult xapi_AudioOpen(const XAudioDesc *desc, XAudioHandle *stream)
{
    return call(XAUDIO_OP_OPEN, (XAudioU64)desc, (XAudioU64)stream);
}

extern "C" XAudioResult xapi_AudioWrite(XAudioHandle stream, const void *pcm, XAudioU32 frames, XAudioI32 ms)
{
    return call(XAUDIO_OP_WRITE, stream, (XAudioU64)pcm, frames, (XAudioU64)(long long)ms);
}

extern "C" XAudioResult xapi_AudioRead(XAudioHandle stream, void *pcm, XAudioU32 frames, XAudioI32 ms)
{
    return call(XAUDIO_OP_READ, stream, (XAudioU64)pcm, frames, (XAudioU64)(long long)ms);
}

extern "C" XAudioResult xapi_AudioControl(XAudioHandle stream, XAudioU32 command)
{
    return call(XAUDIO_OP_CONTROL, stream, command);
}

extern "C" XAudioResult xapi_AudioQuery(XAudioHandle stream, XAudioStatus *status)
{
    return call(XAUDIO_OP_QUERY, stream, (XAudioU64)status);
}

extern "C" XAudioResult xapi_AudioWait(XAudioHandle stream, XAudioU32 mask, XAudioI32 ms, XAudioU32 *events)
{
    return call(XAUDIO_OP_WAIT, stream, mask, (XAudioU64)(long long)ms, (XAudioU64)events);
}

extern "C" XAudioResult xapi_AudioSetVolume(XAudioHandle stream, XAudioU32 volume)
{
    return call(XAUDIO_OP_VOLUME, stream, volume);
}

extern "C" XAudioResult xapi_AudioSetMute(XAudioHandle stream, XAudioU32 muted)
{
    return call(XAUDIO_OP_MUTE, stream, muted);
}

extern "C" XAudioResult xapi_AudioClose(XAudioHandle stream)
{
    return call(XAUDIO_OP_CLOSE, stream);
}

extern "C" XAudioResult xapi_AudioGetDeviceSettings(XAudioU64 device, XAudioDeviceSettings *settings)
{
    return call(XAUDIO_OP_GET_SETTINGS, device, (XAudioU64)settings);
}

extern "C" XAudioResult xapi_AudioSetDeviceSettings(const XAudioDeviceSettings *settings)
{
    if (!settings) return -EINVAL;
    int result = xapiRequestPermissions(user_permissions_from_mask(XPERM_CS));
    if (result != 0) return result > 0 ? -EACCES : result;
    return call(XAUDIO_OP_SET_SETTINGS, (XAudioU64)settings);
}
