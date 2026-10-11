#include "ui/SettingsModeCache.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>

using namespace std::chrono_literals;
using namespace llcv::settings_ui;
static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    std::mutex mutex;
    std::condition_variable changed;
    bool allow = false;
    int calls = 0, completed = 0;
    const auto uiThread = std::this_thread::get_id();
    const ModeQueryKey a{L"card-a",1280,720}, b{L"card-a",1920,1080}, c{L"card-b",3840,2160};
    SettingsModeCache<int> cache([&](const ModeQueryKey& key) {
        Check(std::this_thread::get_id() != uiThread, "driver query stays off UI thread");
        std::unique_lock lock(mutex);
        ++calls; changed.notify_all();
        changed.wait(lock, [&] { return allow; });
        return key.width;
    }, [&] { std::lock_guard lock(mutex); ++completed; changed.notify_all(); });
    auto wait = [&](auto predicate) {
        std::unique_lock lock(mutex);
        Check(changed.wait_for(lock, 2s, predicate), "worker completes within test deadline");
    };
    auto release = [&] { std::lock_guard lock(mutex); allow = true; changed.notify_all(); };
    const auto begin = std::chrono::steady_clock::now();
    Check(!cache.Request(a), "first request is pending");
    Check(std::chrono::steady_clock::now() - begin < 500ms, "UI does not wait for driver");
    wait([&] { return calls == 1; });
    for (int i = 0; i < 100; ++i) Check(!cache.Request(a), "coalesce identical pending request");
    cache.Request(b); cache.Request(c);
    release();
    wait([&] { return completed == 2; });
    Check(calls == 2, "only latest pending resolution is queried");
    for (int i = 0; i < 100; ++i) {
        Check(cache.Request(a) == 1280, "first device cached");
        Check(cache.Request(c) == 3840, "second device cached");
    }
    Check(calls == 2, "cached option changes never rescan");
    cache.Invalidate();
    Check(!cache.Request(c), "manual refresh invalidates cached result");
    wait([&] { return completed == 3; });
    Check(cache.Request(c) == 3840, "fresh result cached");
    { std::lock_guard lock(mutex); allow = false; }
    cache.Invalidate(); cache.Request(a);
    wait([&] { return calls == 4; });
    cache.Invalidate(); cache.Request(c); release();
    wait([&] { return completed == 4; });
    Check(calls == 5 && cache.Request(c) == 3840, "obsolete in-flight query cannot repopulate cache");
    Check(!cache.Request(a), "obsolete first result was discarded");
    wait([&] { return completed == 5; });
    cache.Stop();
    std::puts("PASS: non-blocking requests, 200 cache hits, latest-choice coalescing, refresh epochs and joined shutdown.");
}
