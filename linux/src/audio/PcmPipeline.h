#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace llcv::audio {

class PcmRing {
public:
    explicit PcmRing(size_t capacityFrames);

    size_t Channels() const noexcept { return kChannels; }
    void Push(const int16_t* samples, size_t frames);
    size_t Pop(int16_t* output, size_t frames);
    size_t Discard(size_t frames);
    size_t AvailableFrames() const;
    void Clear();
    uint64_t Overruns() const noexcept { return overruns_.load(std::memory_order_relaxed); }

private:
    static constexpr size_t kChannels = 2;

    const size_t capacityFrames_;
    std::vector<int16_t> data_;
    mutable std::mutex mutex_;
    size_t readFrame_ = 0;
    size_t writeFrame_ = 0;
    size_t available_ = 0;
    std::atomic<uint64_t> overruns_{0};
};

class SincDriftResampler {
public:
    explicit SincDriftResampler(PcmRing& ring) noexcept;

    void Prepare(size_t maxOutputFrames);
    void Reset();
    size_t Render(int16_t* output, size_t outputFrames, double ratio);
    size_t BufferedFrames() const noexcept;

private:
    static constexpr int kHalfTaps = 8;
    static constexpr int kHistoryFrames = kHalfTaps * 2;
    static constexpr double kPi = 3.14159265358979323846;

    static double Sinc(double value) noexcept;
    static double WindowedSinc(double distance) noexcept;
    size_t AppendFromRing(size_t wantedFrames);
    void CompactHistory();

    const size_t channels_;
    PcmRing& ring_;
    std::vector<int16_t> source_;
    std::vector<int16_t> transfer_;
    double position_ = 0.0;
    bool primed_ = false;
};

}
