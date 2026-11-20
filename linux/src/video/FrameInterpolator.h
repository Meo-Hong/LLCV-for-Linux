#pragma once

#include "video/GlApi.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace llcv::video {

enum class InterpolationStatus { Off, Pending, Active, Late, Unsupported, Failed };

struct InterpolationStats {
    uint64_t generated = 0;
    uint64_t late = 0;
    uint64_t busy = 0;
    uint64_t pairs = 0;
    uint64_t gpuJobs = 0;
    double gpuMsSum = 0.0;
    double gpuMsMax = 0.0;
    uint64_t hudJobs = 0;
    uint64_t hudActive = 0;
};

inline constexpr int kInterpolationWidth = 1920;
inline constexpr int kInterpolationHeight = 1080;
inline constexpr int kInterpolationMinFps = 24;
inline constexpr int kInterpolationMaxFps = 60;

constexpr bool InterpolationRateSupported(double fps) {
    return fps >= kInterpolationMinFps - 0.5 && fps <= kInterpolationMaxFps + 0.5;
}

class InterpolationNotices {
public:
    std::optional<InterpolationStatus> Observe(bool enabled, InterpolationStatus frame) {
        if (!enabled) {
            const bool was = enabled_;
            *this = {};
            return was ? std::optional{InterpolationStatus::Off} : std::nullopt;
        }
        if (!enabled_) {
            *this = {};
            enabled_ = true;
        }
        if (frame == InterpolationStatus::Active) {
            misses_ = 0;
            if (problem_ && ++recovered_ < 3) return std::nullopt;
            if (!ready_ || problem_) {
                problem_.reset();
                ready_ = true;
                return InterpolationStatus::Active;
            }
            return std::nullopt;
        }
        recovered_ = 0;
        if (frame == InterpolationStatus::Failed || frame == InterpolationStatus::Unsupported) {
            misses_ = 0;
            if (problem_ != frame) {
                problem_ = frame;
                return frame;
            }
            return std::nullopt;
        }
        if (frame == InterpolationStatus::Late) {
            if (misses_ < 3) ++misses_;
            if (misses_ == 3 && problem_ != frame) {
                problem_ = frame;
                return frame;
            }
            return std::nullopt;
        }
        if (!ready_ && !pending_ && !problem_) {
            pending_ = true;
            return InterpolationStatus::Pending;
        }
        return std::nullopt;
    }

private:
    bool enabled_ = false;
    bool ready_ = false;
    bool pending_ = false;
    unsigned misses_ = 0;
    unsigned recovered_ = 0;
    std::optional<InterpolationStatus> problem_;
};

class FrameInterpolator {
public:
    FrameInterpolator();
    ~FrameInterpolator();
    FrameInterpolator(const FrameInterpolator&) = delete;
    FrameInterpolator& operator=(const FrameInterpolator&) = delete;

    static bool Built();
    static bool ModelsAvailable();

    void Configure(bool hud, bool chroma);
    void Reset();
    GLuint Submit(GLuint source, int width, int height, double fps, int64_t arrivalUs);
    void ScheduleOriginal(double fps);
    void WaitOriginal();
    void MarkUnsupported();
    void MarkOff();

    InterpolationStatus Status() const { return status_; }
    const std::string& Detail() const { return detail_; }
    InterpolationStats Stats() const;
    std::string DeviceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    InterpolationStatus status_ = InterpolationStatus::Off;
    std::string detail_;
};

}
