#include "capture/DirectShowDevices.h"

#include <cstdio>

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, const char* message) {
        if (condition) return;
        std::fprintf(stderr, "FAILED: %s\n", message);
        ++failures;
    };

    // Fail before enumeration: stale USB identity must never be reused.
    std::wstring stalePath = L"previous capture device";
    check(llcv::capture::FindVideoCaptureFilter({}, nullptr, nullptr, nullptr,
              nullptr, &stalePath) == E_POINTER && stalePath.empty(),
          "failed selection clears physical-device path without opening hardware");

    check(llcv::capture::RelatedCaptureAudioScore(
              L"AVerMedia HD Capture GC573", L"AVerMedia HD Capture GC573") ==
              1000,
          "identical normalized names");
    check(llcv::capture::RelatedCaptureAudioScore(
              L"Live Gamer 4K", L"Live Gamer 4K Audio") == 800,
          "one device name contains the other");
    check(llcv::capture::RelatedCaptureAudioScore(
              L"AVerMedia Live Gamer 4K GC573",
              L"AVerMedia GC573 Audio") >= 100,
          "model token matches separate audio filter");
    check(llcv::capture::RelatedCaptureAudioScore(
              L"Unrelated Video Device", L"Different USB Audio") == 0,
          "generic words do not create a false match");

    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed.\n", failures);
        return 1;
    }
    std::puts("DirectShow device tests passed.");
    return 0;
}
