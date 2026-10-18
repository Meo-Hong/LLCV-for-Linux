#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace llcv::recording {

enum class Codec { H264, Hevc, Av1 };
enum class EncoderChoice { AutoHardware, Cpu };
enum class RateControl { Quality, Bitrate };
enum class Quality { Compact, Balanced, High };
enum class SplitMode { Off, Duration, Size };

inline constexpr int kCodecCount = 3;
inline constexpr int kMaxHeightOptions[] = {0, 1440, 1080, 720};
inline constexpr int kMaxFpsOptions[] = {0, 60, 30};
inline constexpr int kVideoMbpsOptions[] = {2, 4, 6, 8, 10, 15, 20, 25, 30, 40, 50, 60, 80, 100};
inline constexpr int kAudioKbpsOptions[] = {128, 192};
inline constexpr int kSplitMinutesOptions[] = {15, 30, 60, 120};
inline constexpr int kSplitGibOptions[] = {1, 2, 4, 8, 16};

struct Options {
    Codec codec = Codec::H264;
    EncoderChoice encoder = EncoderChoice::AutoHardware;
    RateControl rateControl = RateControl::Quality;
    Quality quality = Quality::Balanced;
    int videoMbps = 20;
    int maxHeight = 1080;
    int maxFps = 60;
    int audioKbps = 192;
    int volumePercent = 100;
    SplitMode splitMode = SplitMode::Off;
    int splitMinutes = 30;
    int splitGib = 4;
    std::string folder;
};

struct VideoSize {
    int width = 0;
    int height = 0;
};

inline int QualityQp(Quality quality) {
    switch (quality) {
    case Quality::Compact: return 26;
    case Quality::High: return 20;
    case Quality::Balanced: break;
    }
    return 23;
}

inline VideoSize OutputSize(const Options& options, int width, int height) {
    if (width <= 0 || height <= 0) return {};
    if (options.maxHeight <= 0 || height <= options.maxHeight) return {std::max(2, width & ~1), std::max(2, height & ~1)};
    const int scaled = static_cast<int>(static_cast<int64_t>(width) * options.maxHeight / height);
    return {std::max(2, scaled & ~1), options.maxHeight & ~1};
}

inline uint64_t ReserveBytes(const Options& options) {
    if (options.rateControl == RateControl::Quality) return uint64_t{512} * 1024 * 1024;
    const uint64_t headroom =
        (static_cast<uint64_t>(options.videoMbps) * 1000000 + static_cast<uint64_t>(options.audioKbps) * 1000) * 30 / 8;
    return std::max(uint64_t{256} * 1024 * 1024, headroom);
}

}
