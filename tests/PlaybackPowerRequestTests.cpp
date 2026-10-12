#include "power/PlaybackPowerRequest.h"

#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

using llcv::power::PlaybackPowerRequest;
namespace {
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAILED: %s\n", message); }
}
struct Fixture {
    std::vector<int> calls;
    std::wstring reason;
    bool failCreate = false, failSystem = false, failDisplay = false, failClear = false;
    int live = 0, system = 0, display = 0;
} f;
HANDLE WINAPI Create(PREASON_CONTEXT reason) {
    f.calls.push_back(1);
    Check(reason->Version == POWER_REQUEST_CONTEXT_VERSION &&
          reason->Flags == POWER_REQUEST_CONTEXT_SIMPLE_STRING, "valid localized request reason");
    f.reason = reason->Reason.SimpleReasonString;
    if (f.failCreate) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    ++f.live;
    return reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1));
}
BOOL WINAPI Set(HANDLE, POWER_REQUEST_TYPE type) {
    f.calls.push_back(type == PowerRequestSystemRequired ? 2 : 3);
    Check(type == PowerRequestSystemRequired || type == PowerRequestDisplayRequired,
          "never request away mode or override explicit sleep");
    if ((type == PowerRequestSystemRequired && f.failSystem) ||
        (type == PowerRequestDisplayRequired && f.failDisplay)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (type == PowerRequestSystemRequired) ++f.system; else ++f.display;
    return TRUE;
}
BOOL WINAPI Clear(HANDLE, POWER_REQUEST_TYPE type) {
    f.calls.push_back(type == PowerRequestDisplayRequired ? 4 : 5);
    if (f.failClear) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (type == PowerRequestDisplayRequired) --f.display; else --f.system;
    return TRUE;
}
BOOL WINAPI Close(HANDLE) {
    f.calls.push_back(6);
    Check(f.live == 1, "close exactly one owned request handle");
    --f.live;
    // Closing the object also removes any requests whose Clear failed.
    f.system = f.display = 0;
    return TRUE;
}
const PlaybackPowerRequest::Api api{Create, Set, Clear, Close};
void Balanced() { Check(f.live == 0 && f.system == 0 && f.display == 0, "all requests released"); }
void UnitTests() {
    static_assert(!std::is_copy_constructible_v<PlaybackPowerRequest>);
    static_assert(!std::is_move_constructible_v<PlaybackPowerRequest>);
    for (bool audio : {false, true}) {
        f = {};
        {
            PlaybackPowerRequest request(false, audio, true, api);
            request.Suspend(); request.Resume(); request.Resume();
            request.Reset(); request.Suspend(); request.Resume();
        }
        Check(f.calls.empty(), "disabled option does not call Windows");
    }
    for (bool audio : {false, true}) for (bool english : {false, true}) {
        f = {};
        {
            PlaybackPowerRequest request(true, audio, english, api);
            Check(!request.Error(), "successful request");
            Check(f.live == 1 && f.system == 1 && f.display == (audio ? 0 : 1),
                  "audio allows display sleep; video requires both");
            Check(f.reason == (english ? L"LLCV capture playback is active." : L"LLCV에서 캡처를 재생 중입니다."),
                  "localized reason");
            const auto startupCalls = f.calls.size();
            for (int frame = 0; frame < 100000; ++frame) {
                Check(!request.Error(), "playback does not alter request state");
            }
            Check(f.calls.size() == startupCalls, "no per-frame Windows calls");
            request.Reset();
            const auto stoppedCalls = f.calls.size();
            request.Reset();
            Check(f.calls.size() == stoppedCalls, "reset is idempotent");
        }
        Check(f.calls == (audio ? std::vector<int>{1,2,5,6} : std::vector<int>{1,2,3,4,5,6}),
              "balanced acquire/release ordering; destructor after reset is a no-op");
        Balanced();
    }
    for (int failure = 0; failure < 4; ++failure) {
        f = {};
        f.failCreate = failure == 0; f.failSystem = failure == 1;
        f.failDisplay = failure == 2; f.failClear = failure == 3;
        {
            PlaybackPowerRequest request(true, false, true, api);
            Check((request.Error() != 0) == (failure < 3), "nonfatal acquire failure is reported");
            if (failure == 2) Check(f.system == 1 && f.display == 0, "retain system protection if display request fails");
        }
        Balanced();
        if (failure == 0) Check(f.calls == std::vector<int>{1}, "failed creation has no clear/close");
        if (failure == 1) Check(f.calls == std::vector<int>{1,2,6}, "failed system request is closed immediately");
        if (failure == 2) Check(f.calls == std::vector<int>{1,2,3,5,6}, "only successfully set requests are cleared");
        if (failure == 3) Check(f.calls.back() == 6, "close even if clear fails after user sleep");
    }
    for (bool audio : {false, true}) {
        f = {};
        const auto earlyExit = [audio] {
            PlaybackPowerRequest request(true, audio, false, api);
            return; // stop, capture error, or early return
        };
        earlyExit();
        Balanced();
        try {
            PlaybackPowerRequest request(true, audio, false, api);
            throw 1;
        } catch (int) {}
        Balanced();
        for (int restart = 0; restart < 1000; ++restart) earlyExit();
        Balanced();
    }
}
void LifecycleTests() {
    for (bool audio : {false, true}) for (bool english : {false, true}) {
        f = {};
        {
            PlaybackPowerRequest request(true, audio, english, api);
            const auto startedCalls = f.calls.size();
            request.Resume();
            Check(f.calls.size() == startedCalls, "resume without suspend is a no-op");
            for (int cycle = 0; cycle < 100; ++cycle) {
                request.Suspend();
                Balanced();
                const auto suspendedCalls = f.calls.size();
                request.Suspend();
                Check(f.calls.size() == suspendedCalls, "duplicate suspend is a no-op");
                request.Resume();
                Check(!request.Error() && f.live == 1 && f.system == 1 &&
                          f.display == (audio ? 0 : 1),
                      "user resume restores the original audio/video policy");
                Check(f.reason == (english ? L"LLCV capture playback is active." : L"LLCV에서 캡처를 재생 중입니다."),
                      "resume preserves localized reason");
                const auto resumedCalls = f.calls.size();
                request.Resume();
                for (int frame = 0; frame < 1000; ++frame) {
                    Check(!request.Error(), "resumed playback stays healthy without polling Windows");
                }
                Check(f.calls.size() == resumedCalls, "duplicate resume and playback do not repeat power requests");
            }
            request.Reset();
            Balanced();
            const auto stoppedCalls = f.calls.size();
            request.Resume(); request.Suspend(); request.Resume(); request.Reset();
            Check(f.calls.size() == stoppedCalls, "terminal reset prevents late resume from reactivating playback");
        }
        Balanced();
        f = {};
        {
            PlaybackPowerRequest request(true, audio, english, api);
            request.Suspend();
            const auto suspendedCalls = f.calls.size();
            request.Reset(); request.Resume(); request.Suspend(); request.Resume();
            Check(f.calls.size() == suspendedCalls, "reset while suspended remains terminal");
        }
        Balanced();
    }
    for (bool audio : {false, true}) for (int failure = 0; failure < 3; ++failure) {
        if (audio && failure == 2) continue; // Audio-only never sets DisplayRequired.
        f = {};
        {
            PlaybackPowerRequest request(true, audio, true, api);
            request.Suspend();
            f.failCreate = failure == 0;
            f.failSystem = failure == 1;
            f.failDisplay = failure == 2;
            request.Resume();
            const DWORD expectedError = failure == 0 ? ERROR_ACCESS_DENIED : ERROR_NOT_SUPPORTED;
            Check(request.Error() == expectedError,
                  "resume records the underlying acquire failure");
            Check(f.live == (failure == 2 ? 1 : 0) &&
                      f.system == (failure == 2 ? 1 : 0) && f.display == 0,
                  "failed resume releases incomplete requests, retaining valid system protection");
            const auto failedCalls = f.calls.size();
            request.Resume(); request.Resume();
            Check(f.calls.size() == failedCalls, "failed resume has no busy retry or duplicate retry");
            request.Suspend();
            Balanced();
            f.failCreate = f.failSystem = f.failDisplay = false;
            request.Resume();
            Check(!request.Error() && f.live == 1 && f.system == 1 &&
                      f.display == (audio ? 0 : 1),
                  "a later real sleep/wake cycle can recover from a prior request failure");
        }
        Balanced();
    }
    for (bool audio : {false, true}) {
        f = {};
        {
            PlaybackPowerRequest request(true, audio, true, api);
            // Windows may have already terminated the requests before our
            // capture thread consumes its queued suspend notification.
            f.system = f.display = 0;
            f.failClear = true;
            request.Suspend();
            Balanced();
            f.failClear = false;
            request.Resume();
            Check(!request.Error() && f.system == 1 && f.display == (audio ? 0 : 1),
                  "OS-terminated requests can be safely replaced after user resume");
        }
        Balanced();
        f = {};
        f.failCreate = true;
        {
            PlaybackPowerRequest request(true, audio, true, api);
            const auto failedCalls = f.calls.size();
            request.Resume();
            Check(f.calls.size() == failedCalls, "initial acquisition failure is not retried without suspend");
            request.Suspend();
            f.failCreate = false;
            request.Resume();
            Check(!request.Error() && f.live == 1, "initial failure does not disable a later genuine resume");
        }
        Balanced();
    }
}
void DeferredStartupTests() {
    for (bool enabled : {false, true}) for (bool audio : {false, true})
        for (bool english : {false, true}) {
            f = {};
            {
                PlaybackPowerRequest request(enabled, audio, english, api, true);
                Check(f.calls.empty() && !request.Error(), "deferred construction makes no power request");
                request.Suspend(); request.Suspend();
                Check(f.calls.empty(), "pending startup suspend cannot briefly turn the display on");
                request.Resume();
                if (enabled) {
                    Check(f.live == 1 && f.system == 1 && f.display == (audio ? 0 : 1),
                          "awake startup or user return acquires the deferred policy once");
                    Check(f.reason == (english ? L"LLCV capture playback is active." : L"LLCV에서 캡처를 재생 중입니다."),
                          "deferred startup preserves language");
                } else {
                    Check(f.calls.empty(), "resume cannot enable a disabled deferred request");
                }
                const auto resumedCalls = f.calls.size();
                request.Resume();
                Check(f.calls.size() == resumedCalls, "deferred startup still ignores duplicate resume");
            }
            Balanced();
            f = {};
            {
                PlaybackPowerRequest request(enabled, audio, english, api, true);
                request.Reset(); request.Resume(); request.Suspend(); request.Resume();
            }
            Check(f.calls.empty(), "playback ending before deferred startup never acquires a request");
            f = {};
            { PlaybackPowerRequest request(enabled, audio, english, api, true); }
            Check(f.calls.empty(), "destroying a never-started request makes no Windows calls");
        }
    f = {};
    f.failCreate = true;
    {
        PlaybackPowerRequest request(true, false, true, api, true);
        Check(f.calls.empty() && !request.Error(), "deferred creation failure is not attempted early");
        request.Resume();
        Check(request.Error() == ERROR_ACCESS_DENIED && f.calls == std::vector<int>{1},
              "deferred startup reports the first actual acquire failure");
        request.Resume();
        Check(f.calls == std::vector<int>{1}, "failed deferred startup does not busy retry");
    }
    Balanced();
}
int NativeTests() {
    // Real API smoke check only: no sleep, timeout, registry or policy changes.
    { PlaybackPowerRequest warmup(true, true, true); }
    DWORD before = 0, after = 0;
    Check(GetProcessHandleCount(GetCurrentProcess(), &before) != FALSE, "read handle baseline");
    for (bool audio : {false, true}) {
        for (int restart = 0; restart < 50; ++restart) {
            const bool deferred = (restart & 1) != 0;
            DWORD beforeCreate = 0, afterCreate = 0;
            if (deferred)
                Check(GetProcessHandleCount(GetCurrentProcess(), &beforeCreate) != FALSE,
                      "read deferred startup handle baseline");
            PlaybackPowerRequest request(true, audio, true, deferred);
            if (deferred) {
                Check(GetProcessHandleCount(GetCurrentProcess(), &afterCreate) != FALSE && beforeCreate == afterCreate,
                      "native deferred startup does not create a request handle");
                request.Resume();
            }
            if (request.Error()) {
                std::fprintf(stderr, "Power API unavailable: %lu\n", request.Error());
                return 77;
            }
            for (int cycle = 0; cycle < 4; ++cycle) {
                request.Suspend(); request.Suspend();
                request.Resume(); request.Resume();
                if (request.Error()) {
                    std::fprintf(stderr, "Power API resume unavailable: %lu\n", request.Error());
                    return 77;
                }
            }
            request.Reset();
            request.Resume(); request.Suspend(); request.Resume();
        }
    }
    Check(GetProcessHandleCount(GetCurrentProcess(), &after) != FALSE, "read handle count after playback");
    Check(before == after, "native requests do not leak handles");
    return failures ? 1 : 0;
}
}
int main(int argc, char**) {
    if (argc > 1) return NativeTests();
    UnitTests();
    LifecycleTests();
    DeferredStartupTests();
    std::printf("PlaybackPowerRequestTests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
