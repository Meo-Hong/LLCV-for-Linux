#pragma once

#include <windows.h>
#include <atomic>
#include <cstdint>

namespace llcv::power {

// UI publishes power broadcasts; the capture thread alone owns power requests.
// One auto-reset event joins the existing capture wait, with no frame polling.
class PlaybackPowerEvents final {
public:
    ~PlaybackPowerEvents() { if (event_) CloseHandle(event_); }
    PlaybackPowerEvents() = default;
    PlaybackPowerEvents(const PlaybackPowerEvents&) = delete;
    PlaybackPowerEvents& operator=(const PlaybackPowerEvents&) = delete;

    // Call before creating the viewer/starting its capture thread. The event
    // remains valid until after that thread has joined.
    DWORD Initialize(bool enabled) {
        if (!enabled || event_) return ERROR_SUCCESS;
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return event_ ? ERROR_SUCCESS : GetLastError();
    }
    HANDLE Event() const noexcept { return event_; }

    // Once, for a new request constructed initially suspended. Capture setup
    // may span sleep and an unattended wake: never acquire even briefly if
    // that suspend is already known. Later revisions arrive via Event().
    template<class Request>
    void StartRequest(Request& request, uint64_t& observed) const noexcept {
        observed = state_.load(std::memory_order_acquire);
        if (!(observed & 1)) request.Resume();
    }

    // Only the UI thread publishes. The revision preserves a suspend/resume
    // pair even when both broadcasts arrive before the capture thread wakes.
    void Notify(WPARAM code) noexcept {
        if (!event_) return;
        bool suspended;
        if (code == PBT_APMSUSPEND) suspended = true;
        else if (code == PBT_APMRESUMESUSPEND) suspended = false;
        else return; // Unattended/automatic resume must not keep the PC awake.
        const auto previous = state_.load(std::memory_order_relaxed);
        if (static_cast<bool>(previous & 1) == suspended) return;
        state_.store(((previous + 2) & ~uint64_t{1}) | (suspended ? 1 : 0),
                     std::memory_order_release);
        SetEvent(event_);
    }

    // Called only when Event() is signaled. Force a
    // release before rearming so coalesced broadcasts cannot reuse a request
    // that Windows terminated during sleep. Duplicate wakes are harmless.
    template<class Request>
    bool ApplyPending(Request& request, uint64_t& observed) const noexcept {
        const auto current = state_.load(std::memory_order_acquire);
        if (current == observed) return false;
        observed = current;
        request.Suspend();
        if (!(current & 1)) request.Resume();
        return true;
    }

private:
    HANDLE event_ = nullptr;
    std::atomic<uint64_t> state_{0}; // low bit: suspended; upper bits: revision
};

} // namespace llcv::power
