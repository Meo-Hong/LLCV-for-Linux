#pragma once

#include <windows.h>

namespace llcv::power {

// Owned by the viewer's UI thread. Connected/Modern Standby broadcasts need
// this opt-in even though traditional sleep broadcasts already reach HWNDs.
// Register once after window creation; Reset before that window is destroyed.
class SuspendResumeNotification final {
public:
    struct Api {
        decltype(&RegisterSuspendResumeNotification) subscribe = &RegisterSuspendResumeNotification;
        decltype(&UnregisterSuspendResumeNotification) unsubscribe = &UnregisterSuspendResumeNotification;
    };

    SuspendResumeNotification() = default;
    explicit SuspendResumeNotification(Api api) noexcept : api_(api) {}
    ~SuspendResumeNotification() { Reset(); }
    SuspendResumeNotification(const SuspendResumeNotification&) = delete;
    SuspendResumeNotification& operator=(const SuspendResumeNotification&) = delete;

    // Failures are returned to the owner for nonfatal diagnostics. No retry,
    // timer, power request, or callback thread is created by this subscription.
    DWORD Initialize(HWND window, bool enabled) noexcept {
        if (!enabled) return Reset();
        if (!window) return ERROR_INVALID_WINDOW_HANDLE;
        if (registration_) return window == window_ ? ERROR_SUCCESS : ERROR_ALREADY_EXISTS;
        registration_ = api_.subscribe(window, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (!registration_) return LastError();
        window_ = window;
        return ERROR_SUCCESS;
    }

    DWORD Reset() noexcept {
        if (!registration_) return ERROR_SUCCESS;
        // HPOWERNOTIFY is a subscription, not a kernel handle: never CloseHandle.
        // Keep ownership on failure so a later explicit Reset/destructor can
        // make one bounded cleanup attempt; playback is never blocked/retried.
        if (!api_.unsubscribe(registration_)) return LastError();
        registration_ = nullptr;
        window_ = nullptr;
        return ERROR_SUCCESS;
    }

    bool Registered() const noexcept { return registration_ != nullptr; }
    HWND Window() const noexcept { return window_; }

private:
    static DWORD LastError() noexcept {
        const DWORD error = GetLastError();
        return error ? error : ERROR_GEN_FAILURE;
    }

    Api api_;
    HPOWERNOTIFY registration_ = nullptr;
    HWND window_ = nullptr;
};

} // namespace llcv::power
