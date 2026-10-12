#pragma once

#include <windows.h>

namespace llcv::power {

// Created only after capture starts, on the capture thread. No frame callbacks,
// timers, global power-policy changes, or away-mode requests are involved.
class PlaybackPowerRequest final {
public:
    struct Api {
        decltype(&PowerCreateRequest) create = &PowerCreateRequest;
        decltype(&PowerSetRequest) set = &PowerSetRequest;
        decltype(&PowerClearRequest) clear = &PowerClearRequest;
        decltype(&CloseHandle) close = &CloseHandle;
    };

    PlaybackPowerRequest(bool enabled, bool audioOnly, bool english,
                         bool initiallySuspended = false)
        : PlaybackPowerRequest(enabled, audioOnly, english, Api{}, initiallySuspended) {}

    // A deferred start lets the owner apply power broadcasts received during
    // capture initialization before any request could turn the display on.
    PlaybackPowerRequest(bool enabled, bool audioOnly, bool english, Api api,
                         bool initiallySuspended = false)
        : api_(api), enabled_(enabled), suspended_(initiallySuspended),
          audioOnly_(audioOnly), english_(english) {
        if (enabled_ && !suspended_) Acquire();
    }

    ~PlaybackPowerRequest() { Reset(); }
    PlaybackPowerRequest(const PlaybackPowerRequest&) = delete;
    PlaybackPowerRequest& operator=(const PlaybackPowerRequest&) = delete;

    // Call these on the owning capture thread, after a queued power event.
    // Explicit user sleep invalidates Windows power requests. Recreate only
    // after the user returns, not on an unattended/automatic wake.
    void Suspend() noexcept {
        if (!enabled_ || suspended_) return;
        suspended_ = true;
        Release();
    }
    void Resume() noexcept {
        if (!enabled_ || !suspended_) return;
        suspended_ = false;
        Acquire();
    }

    // Terminal release before stopping a possibly slow device/driver. Queued
    // resume notifications must not reactivate a completed playback session.
    void Reset() noexcept {
        enabled_ = false;
        suspended_ = false;
        Release();
    }
    DWORD Error() const noexcept { return error_; }

private:
    void Acquire() noexcept {
        error_ = ERROR_SUCCESS;
        REASON_CONTEXT reason{};
        reason.Version = POWER_REQUEST_CONTEXT_VERSION;
        reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        reason.Reason.SimpleReasonString = const_cast<PWSTR>(english_
            ? L"LLCV capture playback is active."
            : L"LLCV에서 캡처를 재생 중입니다.");
        handle_ = api_.create(&reason);
        if (!handle_ || handle_ == INVALID_HANDLE_VALUE) {
            RecordError();
            handle_ = nullptr;
            return;
        }
        system_ = api_.set(handle_, PowerRequestSystemRequired) != FALSE;
        if (!system_) {
            RecordError();
            Release();
            return;
        }
        if (!audioOnly_) {
            display_ = api_.set(handle_, PowerRequestDisplayRequired) != FALSE;
            if (!display_) RecordError();
        }
    }

    void Release() noexcept {
        if (!handle_) return;
        if (display_) api_.clear(handle_, PowerRequestDisplayRequired);
        if (system_) api_.clear(handle_, PowerRequestSystemRequired);
        api_.close(handle_);
        handle_ = nullptr;
        system_ = display_ = false;
    }
    void RecordError() noexcept {
        error_ = GetLastError();
        if (!error_) error_ = ERROR_GEN_FAILURE;
    }
    Api api_;
    bool enabled_ = false;
    bool suspended_ = false;
    bool audioOnly_ = false;
    bool english_ = false;
    HANDLE handle_ = nullptr;
    bool system_ = false;
    bool display_ = false;
    DWORD error_ = ERROR_SUCCESS;
};

} // namespace llcv::power
