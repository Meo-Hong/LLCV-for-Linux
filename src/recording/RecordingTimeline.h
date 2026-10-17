#pragma once
#include <mfapi.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <utility>

namespace llcv::recording {
// Worker-only adaptation for fragmented MP4. Windows muxers/readers can
// normalize each track's first timestamp and cannot always infer a fragment's
// final duration from the next fragment. Preserve offsets with a bounded prefix and retain
// exactly one video sample until its true duration is known. Capture callbacks
// and playback never wait for this lookahead or allocate these prefixes.
class RecordingTimeline {
public:
    RecordingTimeline(bool fragmented, DWORD videoStream, DWORD audioStream)
        : fragmented_(fragmented), videoStream_(videoStream), audioStream_(audioStream) {}
    HRESULT Push(IMFSinkWriter* writer, bool video, IMFSample* sample) {
        if (!writer || !sample) return E_POINTER;
        if (!fragmented_) return writer->WriteSample(video ? videoStream_ : audioStream_, sample);
        LONGLONG time = 0, duration = 0;
        HRESULT hr = sample->GetSampleTime(&time);
        if (FAILED(hr)) return hr;
        if (FAILED(hr = sample->GetSampleDuration(&duration))) return hr;
        if (time < 0 || duration <= 0) return MF_E_INVALID_TIMESTAMP;
        if (video) {
            if (pending_) {
                if (time <= pendingTime_) return MF_E_INVALID_TIMESTAMP;
                if (FAILED(hr = pending_->SetSampleDuration(time - pendingTime_))) return hr;
                if (FAILED(hr = Finish(writer))) return hr;
            } else if (firstVideo_) {
                if (time > kMaximumPrefix) return MF_E_INVALID_TIMESTAMP;
                firstVideo_ = false;
                if (time) {
                    Microsoft::WRL::ComPtr<IMFSample> prefix;
                    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
                    if (FAILED(hr = MFCreateSample(&prefix)) ||
                        FAILED(hr = sample->GetBufferByIndex(0, &buffer)) ||
                        FAILED(hr = prefix->AddBuffer(buffer.Get())) ||
                        FAILED(hr = prefix->SetSampleTime(0)) ||
                        FAILED(hr = prefix->SetSampleDuration(time)) ||
                        FAILED(hr = writer->WriteSample(videoStream_, prefix.Get()))) return hr;
                }
            }
            pending_ = sample; pendingTime_ = time;
            return S_OK;
        }
        if (firstAudio_) {
            if (time > kMaximumPrefix) return MF_E_INVALID_TIMESTAMP;
            firstAudio_ = false;
            if (FAILED(hr = PrefixAudio(writer, time))) return hr;
        }
        return writer->WriteSample(audioStream_, sample);
    }
    HRESULT Finish(IMFSinkWriter* writer) {
        if (!writer) return E_POINTER;
        // Consume before WriteSample: a failed write must not be retried during
        // finalization, since an encoder may already have accepted some data.
        auto sample = std::move(pending_);
        return sample ? writer->WriteSample(videoStream_, sample.Get()) : S_OK;
    }
private:
    static constexpr LONGLONG kMaximumPrefix = 5 * 10000000LL;
    HRESULT PrefixAudio(IMFSinkWriter* writer, LONGLONG offset) {
        // 48 kHz stereo PCM16. Round to the nearest whole sample (< 11 us), not
        // an arbitrary A/V correction. At most fifty 100 ms worker writes.
        const LONGLONG frames = (offset * 48000 + 5000000) / 10000000;
        for (LONGLONG first = 0; first < frames;) {
            const DWORD count = static_cast<DWORD>((std::min)(4800LL, frames - first));
            Microsoft::WRL::ComPtr<IMFSample> sample;
            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
            HRESULT hr = MFCreateSample(&sample);
            if (FAILED(hr) || FAILED(hr = MFCreateMemoryBuffer(count * 4, &buffer))) return hr;
            BYTE* data = nullptr;
            if (FAILED(hr = buffer->Lock(&data, nullptr, nullptr))) return hr;
            std::memset(data, 0, count * 4);
            if (FAILED(hr = buffer->Unlock()) || FAILED(hr = buffer->SetCurrentLength(count * 4)) ||
                FAILED(hr = sample->AddBuffer(buffer.Get())) ||
                FAILED(hr = sample->SetSampleTime(first * 10000000 / 48000)) ||
                FAILED(hr = sample->SetSampleDuration((first + count) * 10000000 / 48000 - first * 10000000 / 48000)) ||
                FAILED(hr = writer->WriteSample(audioStream_, sample.Get()))) return hr;
            first += count;
        }
        return S_OK;
    }
    bool fragmented_, firstVideo_ = true, firstAudio_ = true;
    DWORD videoStream_, audioStream_;
    LONGLONG pendingTime_ = 0;
    Microsoft::WRL::ComPtr<IMFSample> pending_;
};
} // namespace llcv::recording
