#pragma once

#include "recording/RecordingOptions.h"

#include <array>
#include <memory>
#include <string>

namespace llcv::recording {

enum class Backend { Nvenc, Vaapi, Cpu };

struct EncoderEntry {
    bool available = false;
    bool quality = false;
    bool bitrate = false;
    Backend backend = Backend::Cpu;
    std::string name;

    bool Supports(RateControl rateControl) const {
        return available && (rateControl == RateControl::Quality ? quality : bitrate);
    }
};

struct CodecSupport {
    EncoderEntry hardware;
    EncoderEntry cpu;

    const EncoderEntry& For(EncoderChoice choice) const { return choice == EncoderChoice::Cpu ? cpu : hardware; }
    bool Any() const { return hardware.available || cpu.available; }
};

enum class ProbeState { Unavailable, NotStarted, Running, Done, Failed };

struct EncoderSupport {
    ProbeState state = ProbeState::NotStarted;
    std::array<CodecSupport, kCodecCount> codecs;
    std::string error;

    const CodecSupport& For(Codec codec) const { return codecs[static_cast<size_t>(codec)]; }
};

bool RecordingAvailable();
const char* BackendName(Backend backend);

class EncoderProbe {
public:
    EncoderProbe();
    ~EncoderProbe();
    EncoderProbe(const EncoderProbe&) = delete;
    EncoderProbe& operator=(const EncoderProbe&) = delete;

    void Start();
    EncoderSupport Snapshot() const;

private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
};

}
