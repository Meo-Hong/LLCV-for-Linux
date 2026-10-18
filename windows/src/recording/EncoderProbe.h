#pragma once
#include "recording/RecordingOptions.h"
#include <windows.h>
#include <array>
#include <algorithm>

struct IMFByteStream;
struct IMFMediaType;
namespace llcv::recording::detail {
// Short, bounded synthetic clip. This is a startup/output smoke test, not a
// throughput benchmark or proof of the encoder's actual quantization behavior.
inline constexpr UINT32 kProbeWidth=1280,kProbeHeight=720,kProbeFps=30;
inline constexpr size_t kProbeFrames=12;
inline constexpr LONGLONG ProbeTime(size_t frame) { return frame*10000000LL/kProbeFps; }
inline constexpr ULONGLONG kMaxProbeBytes=16*1024*1024;

struct ProbeSamples {
    std::array<LONGLONG,kProbeFrames> times{};
    size_t count=0;
    bool Add(LONGLONG time,DWORD bytes) {
        if(!bytes||count==times.size())return false;
        times[count++]=time;return true;
    }
    bool Complete() const {
        if(count!=times.size())return false;
        auto sorted=times;std::sort(sorted.begin(),sorted.end());
        // Encoders may reorder B frames and shift all PTS by a constant delay.
        // MP4 timescale conversion can round; accept up to 1 ms, not a lost frame.
        for(size_t i=1;i<sorted.size();++i){
            // Unsigned subtraction avoids overflow even for malformed PTS at
            // opposite signed limits, without restricting a valid time origin.
            const auto delta=uint64_t(sorted[i])-uint64_t(sorted[0]);
            const auto expected=uint64_t(ProbeTime(i));
            if(delta < expected-10000 || delta > expected+10000)return false;
        }
        return true;
    }
};

// COM/MF must be initialized by the caller. Only called on the discovery worker
// or by tests; never on capture, presentation, or recording callbacks.
HRESULT WriteEncoderProbe(IMFByteStream*,const Options&);
bool IsProbeMediaType(IMFMediaType*,Codec);
HRESULT ValidateEncoderProbe(IMFByteStream*,Codec);
}
