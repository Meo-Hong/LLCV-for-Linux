#pragma once
#include "recording/RecordingStatus.h"
#include <atomic>
#include <cstddef>

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
inline bool ReadyToFinalizeRecording(const std::atomic<bool>& stop,
    const std::atomic<Phase>& phase,const std::atomic<unsigned>& producers,
    size_t videoRead,const std::atomic<size_t>& videoWrite,
    size_t audioRead,const std::atomic<size_t>& audioWrite) noexcept {
    // Stop publishes its flag before closing admission. A producer can still
    // enter in that interval, so observe closed admission before the barrier.
    // Keep the existing sequentially consistent admission/count barrier.
    if(!stop.load() || phase.load()==Phase::Recording ||
        producers.load()!=0)return false;
    // A callback may have published after the worker's earlier empty snapshot
    // and then left. Read the tails again only AFTER all producers have left.
    return videoRead==videoWrite.load(std::memory_order_acquire) &&
        audioRead==audioWrite.load(std::memory_order_acquire);
}
} // namespace llcv::recording
