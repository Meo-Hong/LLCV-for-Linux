#pragma once

#include "capture/FrameMailbox.h"
#include "capture/V4l2Devices.h"
#include "video/MjpegDecoder.h"
#include "video/VideoColor.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace llcv::capture {

struct CaptureConfig {
    std::string devicePath;
    PixelFormat format = PixelFormat::Nv12;
    int width = 1920;
    int height = 1080;
    FrameRate rate;
    video::ColorOverride colorOverride = video::ColorOverride::Auto;
    video::HdrInput hdrInput = video::HdrInput::Auto;
};

enum class CaptureState { Stopped, Streaming, Reconnecting };

struct NegotiatedFormat {
    PixelFormat format = PixelFormat::Nv12;
    int width = 0;
    int height = 0;
    FrameRate rate;
    video::ColorSpec color;
    int bufferCount = 0;
};

struct CaptureCounters {
    std::atomic<uint64_t> frames{0};
    std::atomic<uint64_t> replaced{0};
    std::atomic<uint64_t> skippedInQueue{0};
    std::atomic<uint64_t> driverDropped{0};
    std::atomic<uint64_t> corrupt{0};
    std::atomic<uint64_t> lastFrameNs{0};
    std::atomic<uint64_t> processingNsTotal{0};
    std::atomic<uint64_t> processingSamples{0};
};

class V4l2Capture {
public:
    V4l2Capture() = default;
    ~V4l2Capture();
    V4l2Capture(const V4l2Capture&) = delete;
    V4l2Capture& operator=(const V4l2Capture&) = delete;

    bool Start(const CaptureConfig& config, FrameMailbox& mailbox,
               std::function<void()> onFrame, std::string& error);
    void Stop();

    CaptureState State() const { return state_.load(std::memory_order_acquire); }
    NegotiatedFormat Negotiated() const;
    std::string StatusMessage() const;
    const CaptureCounters& Counters() const { return counters_; }

private:
    struct MappedBuffer {
        void* data = nullptr;
        size_t length = 0;
    };

    bool OpenDevice(std::string& error);
    void CloseDevice();
    void Run();
    bool WaitForStop(int timeoutMs);
    bool DrainAndPublish();
    bool Requeue(uint32_t index);
    bool FillFrame(uint32_t index, uint32_t bytesUsed, VideoFrame& frame);
    void TrackSequence(uint32_t sequence);
    void SetStatus(std::string message);

    CaptureConfig config_;
    FrameMailbox* mailbox_ = nullptr;
    std::function<void()> onFrame_;
    int fd_ = -1;
    int wakeFd_ = -1;
    bool streaming_ = false;
    std::vector<MappedBuffer> buffers_;
    uint32_t bytesPerLine_ = 0;
    bool haveSequence_ = false;
    uint32_t lastSequence_ = 0;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    std::atomic<CaptureState> state_{CaptureState::Stopped};
    CaptureCounters counters_;
    video::MjpegDecoder decoder_;
    mutable std::mutex mutex_;
    NegotiatedFormat negotiated_;
    std::string status_;
};

}
