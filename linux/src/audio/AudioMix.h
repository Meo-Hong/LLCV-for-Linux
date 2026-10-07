#pragma once

#include <cstddef>
#include <cstdint>

namespace llcv::audio {

struct StereoGain {
    double left = 1.0;
    double right = 1.0;
};

struct MixMetrics {
    int peakLeft = 0;
    int peakRight = 0;
    bool clipped = false;
};

MixMetrics ProcessStereoPcm(int16_t* samples, std::size_t frames, StereoGain& current,
                            StereoGain target, bool measurePeaks) noexcept;
int DecayAndHoldPeak(int previous, int observed) noexcept;
double PeakToDbfs(int sample) noexcept;

}
