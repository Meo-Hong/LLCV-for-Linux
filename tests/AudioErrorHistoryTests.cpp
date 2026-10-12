#include "diagnostics/AudioErrorHistory.h"

#include <cstdio>
#include <atomic>
#include <thread>

namespace {
bool Check(bool condition, const char* message) {
    if (condition) return true;
    std::fprintf(stderr, "FAILED: %s\n", message);
    return false;
}
}

int main() {
    using namespace llcv::diagnostics;
    bool ok = true;
    AudioErrorHistory history(4);
    history.Record({1000, 480, AudioErrorKind::Underrun,
                    AudioErrorCause::PcmDepletion});
    history.Record({1015, 480, AudioErrorKind::Underrun,
                    AudioErrorCause::PcmDepletion});
    history.Record({1040, 480, AudioErrorKind::Underrun,
                    AudioErrorCause::InputLate});
    auto stats = history.Analyze(2000, 480, 48000);
    ok &= Check(stats.recentUnderruns == 3 &&
                    stats.maxConsecutiveUnderruns == 2 &&
                    stats.lastEventMs == 1040,
                "audio history must distinguish bursts from isolated errors");

    history.Record({1050, 480, AudioErrorKind::Overrun,
                    AudioErrorCause::Overrun});
    history.Record({1060, 480, AudioErrorKind::Underrun,
                    AudioErrorCause::Resampler});
    stats = history.Analyze(2000, 480, 48000);
    ok &= Check(history.Size() == 4 && stats.recentEvents == 4 &&
                    stats.maxConsecutiveUnderruns == 1,
                "bounded history must evict oldest entries and reset at overrun");

    stats = history.Analyze(400000, 480, 48000);
    ok &= Check(stats.recentEvents == 0 && stats.lastEventMs == 0,
                "events outside the diagnostic window must be ignored");

    AudioErrorHistory minimum(0);
    minimum.Record({0, 480, AudioErrorKind::Underrun, AudioErrorCause::PcmDepletion});
    ok &= Check(minimum.Size() == 0, "zero timestamp is not a recorded error");
    for (uint64_t stamp = 1; stamp <= 1000; ++stamp)
        minimum.Record({stamp, 1, AudioErrorKind::Underrun, AudioErrorCause::PcmDepletion});
    stats = minimum.Analyze(1000, 0, 0);
    ok &= Check(minimum.Size() == 1 && stats.recentEvents == 1 && stats.lastEventMs == 1000,
                "minimum-capacity history wraps without stale events");

    AudioErrorHistory concurrent(10000); // callers cannot expand realtime storage
    std::atomic<bool> finished{false};
    std::thread producer([&] {
        for (uint64_t stamp = 1; stamp <= 100000; ++stamp)
            concurrent.Record({stamp, 1, AudioErrorKind::Underrun, AudioErrorCause::InputLate});
        finished.store(true, std::memory_order_release);
    });
    do {
        stats = concurrent.Analyze(100000, 480, 48000);
        ok &= Check(stats.recentEvents <= AudioErrorHistory::kMaximumCapacity &&
                    stats.maxConsecutiveUnderruns == stats.recentEvents &&
                    stats.lastEventMs <= 100000,
                    "concurrent snapshot stays bounded and preserves chronological burst order");
    } while (!finished.load(std::memory_order_acquire));
    producer.join();
    stats = concurrent.Analyze(100000, 480, 48000);
    ok &= Check(concurrent.Size() == AudioErrorHistory::kMaximumCapacity &&
                stats.recentEvents == AudioErrorHistory::kMaximumCapacity &&
                stats.lastEventMs == 100000,
                "fixed history retains exactly the newest events after many wraps");
    return ok ? 0 : 1;
}
