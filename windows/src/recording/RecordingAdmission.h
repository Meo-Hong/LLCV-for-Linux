#pragma once
#include "recording/RecordingStatus.h"
#include <atomic>

namespace llcv::recording {
// Admission and status share one atomic state. Once Stop wins this transition,
// a worker finishing encoder initialization cannot reopen capture admission.
inline bool TryPublishRecording(std::atomic<Phase>& phase) noexcept {
    auto starting = Phase::Starting;
    return phase.compare_exchange_strong(starting, Phase::Recording);
}
inline void BeginFinalizing(std::atomic<Phase>& phase) noexcept {
    auto current = phase.load();
    while ((current == Phase::Starting || current == Phase::Recording) &&
           !phase.compare_exchange_weak(current, Phase::Finalizing)) {}
}
} // namespace llcv::recording
