#pragma once

#include "audio/AudioEngine.h"
#include "capture/V4l2Capture.h"
#include "recording/RecordingOptions.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace llcv::recording {

enum class Phase { Idle, Starting, Recording, Finalizing };
enum class Failure { None, DiskFull, WriteFailed, Overload, EncoderFailed, StorageUnavailable, FinalizeFailed };
enum class StartError { None, Busy, Unavailable, UnsupportedFormat, HdrInput, StorageUnavailable, DiskFull };

struct VideoInput {
    capture::PixelFormat format = capture::PixelFormat::Nv12;
    int width = 0;
    int height = 0;
    capture::FrameRate rate;
    video::ColorSpec color;
};

struct StartRequest {
    Options options;
    VideoInput video;
    bool audio = false;
    std::filesystem::path folder;
};

struct Status {
    Phase phase = Phase::Idle;
    bool paused = false;
    uint64_t elapsedMs = 0;
    uint64_t bytes = 0;
    uint64_t droppedFrames = 0;
    std::optional<uint64_t> freeBytes;
    int segment = 0;
    std::filesystem::path path;
    std::string encoder;
    int width = 0;
    int height = 0;
    double fps = 0.0;
};

struct Event {
    enum class Kind { Saved, Failed, Empty };
    Kind kind = Kind::Saved;
    Failure failure = Failure::None;
    std::filesystem::path path;
    int segments = 0;
    bool fileKept = false;
    std::string detail;
};

class Recorder final : public capture::RawFrameSink, public audio::CaptureTap {
public:
    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    StartError Start(const StartRequest& request);
    void Stop();
    bool WaitIdle(int timeoutMs);
    void SetPaused(bool paused);
    Status GetStatus() const;
    std::optional<Event> PollEvent();
    bool Busy() const;

    void OnRawFrame(const capture::RawFrame& frame) override;
    void OnCapturedPcm(const int16_t* samples, size_t frames, uint64_t endNs) override;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}
