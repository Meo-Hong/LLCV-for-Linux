#pragma once

#include "capture/VideoFrame.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace llcv::capture {

class FrameMailbox {
public:
    VideoFrame& WriteSlot() { return slots_[back_]; }

    bool Publish() {
        const uint32_t previous = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel);
        back_ = previous & kIndexMask;
        return (previous & kFresh) != 0;
    }

    const VideoFrame* AcquireLatest() {
        if ((middle_.load(std::memory_order_acquire) & kFresh) == 0) return nullptr;
        const uint32_t previous = middle_.exchange(front_, std::memory_order_acq_rel);
        front_ = previous & kIndexMask;
        return &slots_[front_];
    }

    void Reset() {
        back_ = 0;
        middle_.store(1, std::memory_order_release);
        front_ = 2;
    }

private:
    static constexpr uint32_t kFresh = 0x4;
    static constexpr uint32_t kIndexMask = 0x3;

    std::array<VideoFrame, 3> slots_{};
    uint32_t back_ = 0;
    std::atomic<uint32_t> middle_{1};
    uint32_t front_ = 2;
};

}
