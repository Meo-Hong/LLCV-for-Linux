#include "power/SuspendResumeNotification.h"

#include <cstdio>
#include <type_traits>
#include <vector>

using llcv::power::SuspendResumeNotification;

namespace {
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAILED: %s\n", message); }
}

const HWND windowOne = reinterpret_cast<HWND>(static_cast<INT_PTR>(1));
const HWND windowTwo = reinterpret_cast<HWND>(static_cast<INT_PTR>(2));
const HPOWERNOTIFY registration = reinterpret_cast<HPOWERNOTIFY>(static_cast<INT_PTR>(3));
struct Fixture {
    std::vector<int> calls;
    HWND window = nullptr;
    DWORD registerError = ERROR_SUCCESS, unregisterError = ERROR_SUCCESS;
    bool failRegisterWithoutError = false, failUnregisterWithoutError = false;
    bool live = false;
} fixture;

HPOWERNOTIFY WINAPI Subscribe(HANDLE recipient, DWORD flags) {
    fixture.calls.push_back(1);
    Check(flags == DEVICE_NOTIFY_WINDOW_HANDLE, "receive window messages, not concurrent callbacks");
    Check(!fixture.live, "never duplicate an owned subscription");
    if (fixture.registerError || fixture.failRegisterWithoutError) {
        SetLastError(fixture.registerError);
        return nullptr;
    }
    fixture.window = static_cast<HWND>(recipient);
    fixture.live = true;
    return registration;
}

BOOL WINAPI Unsubscribe(HPOWERNOTIFY handle) {
    fixture.calls.push_back(2);
    Check(handle == registration && fixture.live, "release the exact owned subscription");
    if (fixture.unregisterError || fixture.failUnregisterWithoutError) {
        SetLastError(fixture.unregisterError);
        return FALSE;
    }
    fixture.live = false;
    return TRUE;
}
const SuspendResumeNotification::Api api{Subscribe, Unsubscribe};

void UnitTests() {
    static_assert(!std::is_copy_constructible_v<SuspendResumeNotification>);
    static_assert(!std::is_move_constructible_v<SuspendResumeNotification>);
    fixture = {};
    {
        SuspendResumeNotification notifications(api);
        Check(!notifications.Registered() && !notifications.Window(), "default owner is empty");
        for (unsigned count = 0; count < 100; ++count) {
            Check(notifications.Initialize(windowOne, false) == ERROR_SUCCESS,
                  "disabled option is a successful no-op");
            Check(notifications.Reset() == ERROR_SUCCESS, "empty reset is harmless");
        }
        Check(notifications.Initialize(nullptr, false) == ERROR_SUCCESS,
              "disabled option needs no HWND");
    }
    Check(fixture.calls.empty(), "disabled lifetime makes no Windows calls");

    fixture = {};
    {
        SuspendResumeNotification notifications(api);
        Check(notifications.Initialize(nullptr, true) == ERROR_INVALID_WINDOW_HANDLE &&
                  fixture.calls.empty(), "enabled option requires a window");
        Check(notifications.Initialize(windowOne, true) == ERROR_SUCCESS &&
                  notifications.Registered() && notifications.Window() == windowOne &&
                  fixture.window == windowOne, "register the viewer window once");
        for (unsigned count = 0; count < 1000; ++count)
            Check(notifications.Initialize(windowOne, true) == ERROR_SUCCESS,
                  "duplicate initialization does not add a subscription");
        Check(notifications.Initialize(windowTwo, true) == ERROR_ALREADY_EXISTS &&
                  notifications.Window() == windowOne && fixture.calls == std::vector<int>{1},
              "another HWND cannot overwrite the active subscription");
        Check(notifications.Reset() == ERROR_SUCCESS && !notifications.Registered() &&
                  !notifications.Window() && !fixture.live, "explicit reset releases ownership");
        Check(notifications.Reset() == ERROR_SUCCESS, "repeated reset is a no-op");
    }
    Check(fixture.calls == std::vector<int>{1, 2}, "destructor after reset does not unregister twice");

    fixture = {};
    {
        SuspendResumeNotification notifications(api);
        Check(notifications.Initialize(windowOne, true) == ERROR_SUCCESS, "first window registration");
        Check(notifications.Initialize(windowOne, false) == ERROR_SUCCESS && !fixture.live,
              "disabling releases an existing subscription");
        Check(notifications.Initialize(windowTwo, true) == ERROR_SUCCESS &&
                  notifications.Window() == windowTwo, "reinitialize only after release");
    }
    Check(!fixture.live && fixture.calls == std::vector<int>{1, 2, 1, 2},
          "destructor provides fallback cleanup for a new window lifetime");

    fixture = {};
    try {
        SuspendResumeNotification notifications(api);
        Check(notifications.Initialize(windowOne, true) == ERROR_SUCCESS, "exception fixture registration");
        throw 1;
    } catch (int) {}
    Check(!fixture.live && fixture.calls == std::vector<int>{1, 2}, "exception unwinding releases registration");

    for (bool missingError : {false, true}) {
        fixture = {};
        fixture.registerError = missingError ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
        fixture.failRegisterWithoutError = missingError;
        const DWORD expectedError = missingError ? ERROR_GEN_FAILURE : ERROR_ACCESS_DENIED;
        {
            SuspendResumeNotification notifications(api);
            Check(notifications.Initialize(windowOne, true) ==
                      expectedError &&
                      !notifications.Registered() && !notifications.Window(),
                  "register failure is reported without taking ownership");
            Check(notifications.Reset() == ERROR_SUCCESS, "failed registration has nothing to release");
        }
        Check(fixture.calls == std::vector<int>{1} && !fixture.live,
              "failed registration does not retry or unregister a null handle");
    }

    for (bool missingError : {false, true}) {
        fixture = {};
        const DWORD expectedError = missingError ? ERROR_GEN_FAILURE : ERROR_ACCESS_DENIED;
        {
            SuspendResumeNotification notifications(api);
            Check(notifications.Initialize(windowOne, true) == ERROR_SUCCESS, "cleanup failure fixture");
            fixture.unregisterError = missingError ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
            fixture.failUnregisterWithoutError = missingError;
            Check(notifications.Reset() == expectedError &&
                      notifications.Registered() && notifications.Window() == windowOne && fixture.live,
                  "unregister failure is reported without losing ownership");
            Check(notifications.Initialize(windowTwo, true) == ERROR_ALREADY_EXISTS,
                  "failed cleanup cannot leak ownership by replacing the HWND");
            fixture.unregisterError = ERROR_SUCCESS;
            fixture.failUnregisterWithoutError = false;
        }
        Check(!fixture.live && fixture.calls == std::vector<int>{1, 2, 2},
              "destructor can recover a failed explicit unregister once");
    }

    fixture = {};
    {
        SuspendResumeNotification notifications(api);
        for (unsigned cycle = 0; cycle < 1000; ++cycle) {
            Check(notifications.Initialize(windowOne, true) == ERROR_SUCCESS, "repeated window registration");
            Check(notifications.Reset() == ERROR_SUCCESS, "repeated window release");
        }
    }
    Check(!fixture.live && fixture.calls.size() == 2000, "repeated lifetimes remain balanced");
}

HWND nativeWindow = nullptr;
unsigned nativeRegisters = 0, nativeUnregisters = 0;
HPOWERNOTIFY WINAPI NativeSubscribe(HANDLE recipient, DWORD flags) {
    Check(recipient == nativeWindow && IsWindow(nativeWindow), "native registration targets a live window");
    const HPOWERNOTIFY result = RegisterSuspendResumeNotification(recipient, flags);
    if (result) ++nativeRegisters;
    return result;
}
BOOL WINAPI NativeUnsubscribe(HPOWERNOTIFY handle) {
    Check(IsWindow(nativeWindow) != FALSE, "native subscription is released before window destruction");
    const BOOL result = UnregisterSuspendResumeNotification(handle);
    if (result) ++nativeUnregisters;
    return result;
}

int NativeTests() {
    // Hidden top-level window only. No broadcasts are sent and no sleep,
    // display, execution-state, registry, or power-policy changes are made.
    nativeWindow = CreateWindowExW(0, L"STATIC", L"LLCV power subscription test",
                                  WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    if (!nativeWindow) {
        std::fprintf(stderr, "Cannot create a hidden native window: %lu\n", GetLastError());
        return 77;
    }
    Check(!IsWindowVisible(nativeWindow), "native test never shows a window");
    {
        SuspendResumeNotification notifications({NativeSubscribe, NativeUnsubscribe});
        Check(notifications.Initialize(nativeWindow, false) == ERROR_SUCCESS &&
                  nativeRegisters == 0, "native disabled path does not subscribe");
        for (unsigned cycle = 0; cycle < 100; ++cycle) {
            const DWORD error = notifications.Initialize(nativeWindow, true);
            if (error) {
                std::fprintf(stderr, "Native suspend/resume registration failed: %lu\n", error);
                ++failures;
                break;
            }
            Check(notifications.Initialize(nativeWindow, true) == ERROR_SUCCESS &&
                      nativeRegisters == cycle + 1, "native duplicate initialization is harmless");
            const DWORD cleanupError = notifications.Reset();
            if (cleanupError) {
                std::fprintf(stderr, "Native suspend/resume unregister failed: %lu\n", cleanupError);
                ++failures;
                break;
            }
        }
    }
    Check(nativeRegisters == 100 && nativeUnregisters == 100, "native registrations and releases are balanced");
    Check(DestroyWindow(nativeWindow) != FALSE, "destroy hidden window after releasing subscriptions");
    nativeWindow = nullptr;
    std::printf("SuspendResumeNotificationNativeTests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
}

int main(int argc, char**) {
    if (argc > 1) return NativeTests();
    UnitTests();
    std::printf("SuspendResumeNotificationTests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
