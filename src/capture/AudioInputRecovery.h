#pragma once

#include <cstdint>

namespace llcv::capture {

// Packet activity, never signal amplitude: silent PCM is healthy input.
// Used only by the opt-in diagnostic audio-only graph owner, never the regular
// application. Algorithm-only tests may include this header independently.
// No work is added to audio callbacks.
class AudioInputWatchdog {
public:
    static constexpr uint64_t StartupTimeoutMs = 10000;
    static constexpr uint64_t StallTimeoutMs = 5000;
    enum class Timeout { None, Startup, Stalled };

    explicit AudioInputWatchdog(uint64_t startedMs) : startedMs_(startedMs) {}

    Timeout Check(uint64_t nowMs, uint64_t lastPacketMs) const noexcept {
        const bool received = lastPacketMs != 0 && lastPacketMs >= startedMs_;
        const uint64_t since = received ? lastPacketMs : startedMs_;
        const uint64_t limit = received ? StallTimeoutMs : StartupTimeoutMs;
        // A callback can arrive after the caller sampled its clock.
        if (nowMs < since || nowMs - since < limit) return Timeout::None;
        return received ? Timeout::Stalled : Timeout::Startup;
    }

private:
    uint64_t startedMs_;
};

enum class AudioInputAttempt { Complete, Failed, InputTimeout };
inline constexpr unsigned AudioInputRetryLimit = 3;

// Attempt must stop/detach/release its graph before returning. Only missing
// packets are retried; device-loss, format rejection and EOS keep their normal
// handling. The budget is per viewer session, not reset by one stray packet.
template<class Attempt, class Running, class Wait, class Notify>
AudioInputAttempt RunAudioInputRecovery(Attempt attempt, Running running,
                                       Wait wait, Notify notify) {
    for (unsigned retry = 0; running(); ++retry) {
        const auto result = attempt();
        if (!running()) return AudioInputAttempt::Complete;
        if (result != AudioInputAttempt::InputTimeout || retry == AudioInputRetryLimit)
            return result;
        notify(retry + 1);
        if (!wait(250u << retry)) return AudioInputAttempt::Complete;
    }
    return AudioInputAttempt::Complete;
}

} // namespace llcv::capture
