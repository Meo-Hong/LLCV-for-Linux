#include "recording/EncoderSupport.h"
#include "recording/Recorder.h"

namespace llcv::recording {

struct EncoderProbe::Shared {};

struct Recorder::Impl {};

bool RecordingAvailable() {
    return false;
}

const char* BackendName(Backend backend) {
    switch (backend) {
    case Backend::Nvenc: return "NVENC";
    case Backend::Vaapi: return "VA-API";
    case Backend::Cpu: break;
    }
    return "CPU";
}

EncoderProbe::EncoderProbe() = default;

EncoderProbe::~EncoderProbe() = default;

void EncoderProbe::Start() {}

EncoderSupport EncoderProbe::Snapshot() const {
    EncoderSupport support;
    support.state = ProbeState::Unavailable;
    return support;
}

Recorder::Recorder() = default;

Recorder::~Recorder() = default;

StartError Recorder::Start(const StartRequest&) {
    return StartError::Unavailable;
}

void Recorder::Stop() {}

bool Recorder::WaitIdle(int) {
    return true;
}

void Recorder::SetPaused(bool) {}

Status Recorder::GetStatus() const {
    return {};
}

std::optional<Event> Recorder::PollEvent() {
    return std::nullopt;
}

bool Recorder::Busy() const {
    return false;
}

void Recorder::OnRawFrame(const capture::RawFrame&) {}

void Recorder::OnCapturedPcm(const int16_t*, size_t, uint64_t) {}

}
