#include "recording/EncoderSupport.h"

#include "diagnostics/Logger.h"
#include "recording/FfmpegEncoder.h"

#include <exception>
#include <initializer_list>
#include <mutex>
#include <thread>

namespace llcv::recording {
namespace {

bool TryOpen(Codec codec, Backend backend, RateControl rateControl) {
    ffmpeg::VideoEncoderConfig config;
    config.codec = codec;
    config.backend = backend;
    config.rateControl = rateControl;
    ffmpeg::VideoEncoder encoder;
    std::string error;
    if (ffmpeg::OpenVideoEncoder(config, encoder, error)) return true;
    diagnostics::Log("[recording] probe %s (%s): %s", ffmpeg::EncoderName(codec, backend),
                     rateControl == RateControl::Quality ? "CQP" : "bitrate", error.c_str());
    return false;
}

EncoderEntry Probe(Codec codec, Backend backend) {
    EncoderEntry entry;
    entry.backend = backend;
    entry.name = ffmpeg::EncoderName(codec, backend);
    entry.quality = TryOpen(codec, backend, RateControl::Quality);
    entry.bitrate = TryOpen(codec, backend, RateControl::Bitrate);
    entry.available = entry.quality || entry.bitrate;
    return entry;
}

EncoderSupport RunProbe() {
    EncoderSupport support;
    for (int index = 0; index < kCodecCount; ++index) {
        const auto codec = static_cast<Codec>(index);
        CodecSupport& result = support.codecs[static_cast<size_t>(index)];
        for (const Backend backend : {Backend::Nvenc, Backend::Vaapi}) {
            EncoderEntry entry = Probe(codec, backend);
            if (entry.available) {
                result.hardware = entry;
                break;
            }
        }
        result.cpu = Probe(codec, Backend::Cpu);
        diagnostics::Log("[recording] %s: hardware %s, cpu %s", ffmpeg::EncoderName(codec, Backend::Cpu),
                         result.hardware.available ? result.hardware.name.c_str() : "none",
                         result.cpu.available ? result.cpu.name.c_str() : "none");
    }
    support.state = ProbeState::Done;
    return support;
}

}

struct EncoderProbe::Shared {
    mutable std::mutex mutex;
    EncoderSupport support;
    std::thread thread;
};

bool RecordingAvailable() {
    return true;
}

const char* BackendName(Backend backend) {
    switch (backend) {
    case Backend::Nvenc: return "NVENC";
    case Backend::Vaapi: return "VA-API";
    case Backend::Cpu: break;
    }
    return "CPU";
}

EncoderProbe::EncoderProbe() : shared_(std::make_shared<Shared>()) {}

EncoderProbe::~EncoderProbe() {
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        finished = shared_->support.state != ProbeState::Running;
    }
    if (!shared_->thread.joinable()) return;
    if (finished) {
        shared_->thread.join();
    } else {
        shared_->thread.detach();
    }
}

void EncoderProbe::Start() {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (shared_->support.state == ProbeState::Running || shared_->support.state == ProbeState::Done) return;
    if (shared_->thread.joinable()) shared_->thread.join();
    shared_->support = EncoderSupport{};
    shared_->support.state = ProbeState::Running;
    std::shared_ptr<Shared> keep = shared_;
    shared_->thread = std::thread([keep] {
        EncoderSupport result;
        try {
            result = RunProbe();
        } catch (const std::exception& exception) {
            result.state = ProbeState::Failed;
            result.error = exception.what();
        }
        std::lock_guard<std::mutex> resultLock(keep->mutex);
        keep->support = std::move(result);
    });
}

EncoderSupport EncoderProbe::Snapshot() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->support;
}

}
