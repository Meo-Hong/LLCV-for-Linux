#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

namespace llcv::recording {
enum class Codec : int { H264, Hevc, Av1 };
enum class RateControl : int { Quality, Bitrate };
enum class Quality : int { Compact, Balanced, High };
enum class SplitMode : int { None, Time, Size };
inline int QualityQp(Quality quality) { return quality==Quality::Compact?26:quality==Quality::High?20:23; }
struct Options {
    Codec codec=Codec::H264;
    // Empty = automatic hardware only; "cpu" = explicit software selection.
    std::wstring encoder;
    int maxFps=60;
    int maxHeight=1080;
    SplitMode splitMode=SplitMode::None;
    int splitSeconds=1800,splitMiB=4096;
    int videoMbps=20;
    RateControl rateControl=RateControl::Quality;
    Quality quality=Quality::Balanced;
    int audioKbps=192;
    int audioVolumePercent=100;
    std::wstring folder;
    bool operator==(const Options&) const = default;
};
inline bool Valid(const Options& o) {
    return static_cast<int>(o.codec)>=0 && static_cast<int>(o.codec)<=2 &&
        (o.maxFps==0 || o.maxFps==30 || o.maxFps==60) &&
        (o.maxHeight==0 || o.maxHeight==720 || o.maxHeight==1080 || o.maxHeight==1440) &&
        static_cast<int>(o.splitMode)>=0 && static_cast<int>(o.splitMode)<=2 &&
        o.splitSeconds>=1 && o.splitSeconds<=86400 && o.splitMiB>=1 && o.splitMiB<=1048576 &&
        o.videoMbps>=2 && o.videoMbps<=100 &&
        static_cast<int>(o.rateControl)>=0 && static_cast<int>(o.rateControl)<=1 &&
        static_cast<int>(o.quality)>=0 && static_cast<int>(o.quality)<=2 &&
        (o.audioKbps==128 || o.audioKbps==192) &&
        o.audioVolumePercent>=0 && o.audioVolumePercent<=100 &&
        (o.encoder.empty() || o.encoder==L"cpu") &&
        o.folder.find_first_of(L"\r\n")==std::wstring::npos &&
        o.folder.find(L'\0')==std::wstring::npos;
}
inline int OutputFps(const Options& o,int source) { return o.maxFps?std::min(source,o.maxFps):source; }
struct VideoSize { int width,height; };
inline VideoSize OutputSize(const Options& o,int width,int height) {
    if(!o.maxHeight || height<=o.maxHeight)return {width,height};
    const int scaled=static_cast<int>(static_cast<int64_t>(width)*o.maxHeight/height);
    return {std::max(2,scaled&~1),o.maxHeight};
}
// Keep at least 256 MiB or 30 seconds of bitrate headroom for mux finalization.
inline uint64_t ReserveBytes(const Options& o) {
    // CQP has no bitrate ceiling. Keep a larger reserve; disk/write failures
    // still stop recording without waiting on the playback thread.
    if(o.rateControl==RateControl::Quality)return uint64_t{512}*1024*1024;
    const auto headroom=(uint64_t(o.videoMbps)*1000000+uint64_t(o.audioKbps)*1000)*30/8;
    return std::max(uint64_t{256}*1024*1024,headroom);
}
struct FrameLimiter {
    int fps=0,inputFps=0;
    int64_t first=-1,lastBucket=-1;
    bool Accept(int64_t time) {
        if(!fps) return true;
        if(first<0)first=time;
        if(time<first)return false;
        // Driver timestamps truncate fractional 100 ns ticks (e.g. 1/30 s).
        // Recover that sub-tick rounding without moving a real frame boundary.
        // Select the nearest input frame to each output tick. Small callback
        // jitter must not alternate 1-frame / 3-frame skips at 120 -> 60 fps.
        const int64_t tolerance=inputFps>fps?5000000LL/std::max(1,inputFps):0;
        const auto bucket=((time-first+tolerance)*fps+fps-1)/10000000;
        if(bucket<=lastBucket)return false;
        lastBucket=bucket;return true;
    }
};
std::wstring DefaultFolder();
std::wstring ProfilePath();
Options LoadOptions(const std::wstring& path=ProfilePath());
bool SaveOptions(const Options&,const std::wstring& path=ProfilePath());
inline Options sessionOptions;
}
