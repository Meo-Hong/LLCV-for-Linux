#include "power/PlaybackPowerEvents.h"
#include "capture/CaptureGraphEvents.h"
#include <cstdio>
#include <thread>

static int failures = 0;
static void Check(bool ok, const char* message) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAILED: %s\n", message); }
}
#include "CaptureGraphEventsTests.inl"

struct Request {
    int suspends = 0, resumes = 0;
    bool active = true, stopped = false;
    void Suspend() { ++suspends; active = false; }
    void Resume() { ++resumes; if (!stopped) active = true; }
    void Reset() { stopped = true; active = false; }
};

static void TestBroadcasts() {
    using llcv::power::PlaybackPowerEvents;
    PlaybackPowerEvents disabled;
    Check(disabled.Initialize(false) == ERROR_SUCCESS && !disabled.Event(),
          "disabled option adds no wait handle");
    disabled.Notify(PBT_APMSUSPEND);
    Request idle;
    uint64_t revision = 0;
    Check(!disabled.ApplyPending(idle, revision) && idle.suspends == 0,
          "disabled option ignores broadcasts");

    PlaybackPowerEvents events;
    Check(events.Initialize(true) == ERROR_SUCCESS && events.Event(), "create lifecycle wake");
    const HANDLE handle = events.Event();
    Check(events.Initialize(true) == ERROR_SUCCESS && events.Event() == handle,
          "initialization is idempotent");
    Request request;
    Check(!events.ApplyPending(request, revision), "startup does not reacquire a fresh request");
    events.Notify(PBT_APMRESUMEAUTOMATIC);
    events.Notify(PBT_APMRESUMESUSPEND);
    events.Notify(PBT_APMPOWERSTATUSCHANGE);
    Check(WaitForSingleObject(handle, 0) == WAIT_TIMEOUT,
          "automatic, duplicate resume, and status events do not wake capture");
    events.Notify(PBT_APMSUSPEND);
    Check(WaitForSingleObject(handle, 0) == WAIT_OBJECT_0 &&
          events.ApplyPending(request, revision) && !request.active && request.suspends == 1,
          "suspend releases on capture thread");
    events.Notify(PBT_APMSUSPEND);
    events.Notify(PBT_APMRESUMEAUTOMATIC);
    Check(WaitForSingleObject(handle, 0) == WAIT_TIMEOUT &&
          !events.ApplyPending(request, revision) && request.resumes == 0,
          "duplicate suspend and unattended wake never rearm");
    events.Notify(PBT_APMRESUMESUSPEND);
    Check(WaitForSingleObject(handle, 0) == WAIT_OBJECT_0 &&
          events.ApplyPending(request, revision) && request.active && request.resumes == 1,
          "user return rearms once");
    events.Notify(PBT_APMRESUMESUSPEND);
    Check(WaitForSingleObject(handle, 0) == WAIT_TIMEOUT && !events.ApplyPending(request, revision),
          "duplicate user resume has no extra work");
    for (int cycle = 0; cycle < 1000; ++cycle) {
        events.Notify(PBT_APMSUSPEND);
        events.Notify(PBT_APMRESUMEAUTOMATIC);
        events.Notify(PBT_APMRESUMESUSPEND);
        Check(WaitForSingleObject(handle, 0) == WAIT_OBJECT_0 &&
              events.ApplyPending(request, revision) && request.active,
              "coalesced suspend/resume still invalidates the old request");
    }
    Check(request.resumes == 1001, "one rearm per coalesced cycle");
    events.Notify(PBT_APMSUSPEND);
    events.Notify(PBT_APMRESUMESUSPEND);
    events.Notify(PBT_APMSUSPEND);
    Check(WaitForSingleObject(handle, 0) == WAIT_OBJECT_0 &&
          events.ApplyPending(request, revision) && !request.active && request.resumes == 1001,
          "newer suspend wins over an unconsumed resume");

    // Producer can overtake the consumer: the final revision must still be
    // observed, with no blocking, heap allocation, or lost wakeup on Notify.
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (int cycle = 0; cycle < 5000; ++cycle) {
            events.Notify(PBT_APMRESUMESUSPEND);
            events.Notify(PBT_APMSUSPEND);
        }
        events.Notify(PBT_APMRESUMESUSPEND);
        done.store(true, std::memory_order_release);
    });
    while (!done.load(std::memory_order_acquire)) {
        if (WaitForSingleObject(handle, 1) == WAIT_OBJECT_0) events.ApplyPending(request, revision);
    }
    producer.join();
    events.ApplyPending(request, revision);
    Check(request.active, "final user-resume revision survives concurrent coalescing");
}

static void TestCompletionBeforeResume() {
    using llcv::capture::CaptureGraphEvents;
    for (bool video : {false, true}) {
        auto* source = new TestGraphEvents;
        CaptureGraphEvents graph;
        Check(graph.Initialize(static_cast<IMediaEvent*>(source)) == S_OK, "initialize EOS fixture");
        llcv::power::PlaybackPowerEvents power;
        Check(power.Initialize(true) == ERROR_SUCCESS, "initialize EOS wake");
        Request request;
        uint64_t revision = 0;
        power.Notify(PBT_APMSUSPEND);
        power.Notify(PBT_APMRESUMESUSPEND);
        source->Push(EC_COMPLETE, S_OK);
        const HANDLE frame = video ? CreateEventW(nullptr, FALSE, TRUE, nullptr) : nullptr;
        Check(graph.Wait(frame, 0, power.Event()) == S_OK && graph.Completed(),
              "normal stream completion wins over resume and ready frame");
        if (graph.Completed()) request.Reset();
        else if (graph.LastWaitReason() == CaptureGraphEvents::WaitReason::Wake)
            power.ApplyPending(request, revision);
        Check(request.stopped && !request.active && request.resumes == 0,
              "completion exits playback without rearming power");
        graph.Reset();
        source->Release();
        if (frame) CloseHandle(frame);
    }
}

static void TestStartupDuringSleep() {
    for (bool automaticWake : {false, true}) {
        llcv::power::PlaybackPowerEvents events;
        Check(events.Initialize(true) == ERROR_SUCCESS, "initialize deferred startup fixture");
        events.Notify(PBT_APMSUSPEND);
        if (automaticWake) events.Notify(PBT_APMRESUMEAUTOMATIC);
        Request request;
        request.active = false; // Deferred construction has not called PowerSetRequest.
        uint64_t revision = 0;
        events.StartRequest(request, revision);
        Check(!request.active && request.suspends == 0 && request.resumes == 0,
              "startup during suspend or unattended wake never briefly acquires power");
        Check(WaitForSingleObject(events.Event(), 0) == WAIT_OBJECT_0 &&
              !events.ApplyPending(request, revision), "startup consumes the same snapshot only once");
        events.Notify(PBT_APMRESUMESUSPEND);
        Check(WaitForSingleObject(events.Event(), 0) == WAIT_OBJECT_0 &&
              events.ApplyPending(request, revision) && request.active && request.resumes == 1,
              "user return after deferred startup acquires once");
    }
    for (bool completedSleepCycle : {false, true}) {
        llcv::power::PlaybackPowerEvents events;
        Check(events.Initialize(true) == ERROR_SUCCESS, "initialize awake startup fixture");
        if (completedSleepCycle) {
            events.Notify(PBT_APMSUSPEND);
            events.Notify(PBT_APMRESUMESUSPEND);
        }
        Request request;
        request.active = false;
        uint64_t revision = 0;
        events.StartRequest(request, revision);
        Check(request.active && request.resumes == 1 && request.suspends == 0,
              "awake startup acquires exactly once, including an already completed sleep cycle");
        events.ApplyPending(request, revision);
        Check(request.resumes == 1, "pending pre-start wake does not reacquire the new request");
    }
}

int main() {
    TestBroadcasts();
    TestStartupDuringSleep();
    TestCompletionBeforeResume();
    TestCaptureGraphEvents();
    std::printf("PlaybackPowerEventsTests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
