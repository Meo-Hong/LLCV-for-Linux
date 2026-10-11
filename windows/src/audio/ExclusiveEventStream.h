#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>

namespace llcv::audio_device {

// Shared by the preflight and renderer: an alignment failure consumes the
// connection, so retry once on a newly activated client with the actual size.
inline HRESULT InitializeExclusiveEvent(IMMDevice* device, IAudioClient*& client,
                                        const WAVEFORMATEX& format,
                                        REFERENCE_TIME duration) {
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK, duration, duration, &format, nullptr);
    if (hr != AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) return hr;
    UINT32 frames = 0;
    hr = client->GetBufferSize(&frames);
    if (FAILED(hr)) return hr;
    if (!frames || !format.nSamplesPerSec) return E_INVALIDARG;
    duration = static_cast<REFERENCE_TIME>(
        (10'000'000ULL * frames + format.nSamplesPerSec / 2) / format.nSamplesPerSec);
    client->Release();
    client = nullptr;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
                          reinterpret_cast<void**>(&client));
    if (FAILED(hr)) return hr;
    return client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK, duration, duration, &format, nullptr);
}

inline HRESULT SubmitExclusiveSilence(IAudioRenderClient* render, UINT32 frames) {
    BYTE* bytes = nullptr;
    const HRESULT hr = render->GetBuffer(frames, &bytes);
    return FAILED(hr) ? hr : render->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
}

} // namespace llcv::audio_device
