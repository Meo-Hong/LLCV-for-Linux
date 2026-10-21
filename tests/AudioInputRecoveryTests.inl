// Deterministic recovery orchestration plus real production sample callbacks.
// No device is enumerated/opened and no wall-clock timeout is needed.
static int TestAudioInputRecovery() {
    using namespace llcv::capture;
    using Timeout = AudioInputWatchdog::Timeout;
    AudioInputWatchdog watchdog(1000);
    Require(watchdog.Check(10999, 0) == Timeout::None &&
            watchdog.Check(11000, 0) == Timeout::Startup,
            "initial no-input timeout boundary");
    Require(watchdog.Check(11000, 999) == Timeout::Startup,
            "previous graph timestamp cannot hide a failed new graph");
    Require(watchdog.Check(5999, 1000) == Timeout::None &&
            watchdog.Check(6000, 1000) == Timeout::Stalled,
            "packet inside Run counts; later stall has its own boundary");
    Require(watchdog.Check(500, 0) == Timeout::None &&
            watchdog.Check(1000, 1001) == Timeout::None,
            "clock sampled before a callback must not underflow");
    for (uint64_t t = 1000; t < 36001000; t += 100) {
        Require(watchdog.Check(t, t) == Timeout::None,
                "ten hours of quiet packets do not trigger recovery");
    }

    for (unsigned timeouts = 0; timeouts <= 4; ++timeouts) {
        unsigned attempts = 0, notices = 0;
        std::vector<unsigned> delays;
        bool released = true;
        const auto result = RunAudioInputRecovery([&] {
            Require(released, "old attempt is gone before creating its replacement");
            released = false;
            const auto outcome = attempts++ < timeouts
                ? AudioInputAttempt::InputTimeout : AudioInputAttempt::Complete;
            released = true;
            return outcome;
        }, [] { return true; }, [&](unsigned delay) {
            Require(released, "backoff starts after graph release");
            delays.push_back(delay); return true;
        }, [&](unsigned retry) { Require(retry == ++notices, "ordered retry notification"); });
        Require(attempts == (std::min)(timeouts + 1, 4u) &&
                notices == (std::min)(timeouts, 3u) &&
                result == (timeouts == 4 ? AudioInputAttempt::InputTimeout : AudioInputAttempt::Complete),
                "successful startup/recovery and exhausted retries");
        for (size_t i = 0; i < delays.size(); ++i)
            Require(delays[i] == (250u << i), "bounded backoff, not a busy retry loop");
    }
    for (auto terminal : {AudioInputAttempt::Complete, AudioInputAttempt::Failed}) {
        unsigned attempts = 0, unexpectedWaits = 0, unexpectedNotices = 0;
        const auto result = RunAudioInputRecovery([&] { ++attempts; return terminal; },
            [] { return true; }, [&](unsigned) { ++unexpectedWaits; return false; },
            [&](unsigned) { ++unexpectedNotices; });
        Require(result == terminal && attempts == 1 && unexpectedWaits == 0 && unexpectedNotices == 0,
                "normal completion, format rejection and device errors do not retry");
    }
    for (int cancelPoint : {0, 1, 2}) {
        bool running = cancelPoint != 0;
        unsigned attempts = 0, waits = 0;
        const auto result = RunAudioInputRecovery([&] {
            ++attempts;
            if (cancelPoint == 1) running = false;
            return AudioInputAttempt::InputTimeout;
        }, [&] { return running; }, [&](unsigned) {
            ++waits; running = false; return false;
        }, [](unsigned) {});
        Require(result == AudioInputAttempt::Complete && attempts == (cancelPoint == 0 ? 0u : 1u) &&
                waits == (cancelPoint == 2 ? 1u : 0u),
                "close/settings cancellation does not open another capture device");
    }
    // An open failure after a no-input retry must preserve that failure.
    unsigned calls = 0;
    Require(RunAudioInputRecovery([&] { return ++calls == 1 ? AudioInputAttempt::InputTimeout
                    : AudioInputAttempt::Failed; }, [] { return true; },
                [](unsigned) { return true; }, [](unsigned) {}) == AudioInputAttempt::Failed && calls == 2,
            "failed rebuild is not mistaken for another no-input timeout");

    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "sample allocator COM");
    Microsoft::WRL::ComPtr<IMemAllocator> allocator;
    Require(SUCCEEDED(CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&allocator))), "real DirectShow allocator");
    ALLOCATOR_PROPERTIES requested{1, 480 * 4, 1, 0}, actual{};
    Require(SUCCEEDED(allocator->SetProperties(&requested, &actual)) &&
            SUCCEEDED(allocator->Commit()), "PCM sample allocation");
    Microsoft::WRL::ComPtr<IMediaSample> sample;
    Require(SUCCEEDED(allocator->GetBuffer(&sample, nullptr, nullptr, 0)), "PCM sample");
    BYTE* pcm = nullptr;
    Require(SUCCEEDED(sample->GetPointer(&pcm)), "PCM pointer");
    memset(pcm, 0, requested.cbBuffer);

    const auto savedTracking = g_audioTrackingStartMs.load();
    const auto savedLast = g_audioLastCaptureCallbackMs.load();
    const auto savedRejected = g_captureAudioRejected.load();
    const auto savedManual = g_playbackMuted.load(), savedBackground = g_backgroundAudioMuted.load();
    g_audioTrackingStartMs = UINT64_MAX; // recovery must work during diagnostics warmup
    g_captureAudioRejected = false;
    g_ring.ConfigureChannels(2);
    llcv::capture_audio::Format format{};
    format.channels = 2; format.containerBits = format.validBits = 16; format.blockAlign = 4;
    format.path = llcv::capture_audio::Path::Direct16BitStereo;
    auto* callback = CreateAudioSampleCallback(format);
    const auto started = GetTickCount64();
    for (bool muted : {false, true}) {
        g_playbackMuted = muted; g_backgroundAudioMuted = muted;
        g_audioLastCaptureCallbackMs = 0;
        sample->SetActualDataLength(0);
        Require(SUCCEEDED(callback->SampleCB(0, sample.Get())) && g_audioLastCaptureCallbackMs == 0,
                "empty samples cannot keep dead input alive");
        sample->SetActualDataLength(3);
        Require(SUCCEEDED(callback->SampleCB(0, sample.Get())) && g_audioLastCaptureCallbackMs == 0,
                "unaligned samples cannot keep dead input alive");
        sample->SetActualDataLength(requested.cbBuffer);
        Require(SUCCEEDED(callback->SampleCB(0, sample.Get())) &&
                g_audioLastCaptureCallbackMs >= started && g_audioLastCaptureCallbackMs != 0,
                "silent valid PCM signals life even when muted and diagnostics are warming up");
        Require(AudioInputWatchdog(started).Check(GetTickCount64(), g_audioLastCaptureCallbackMs) == Timeout::None,
                "production callback feeds the exact timestamp used by the watchdog");
    }
    callback->Release();
    g_ring.Clear();
    g_audioTrackingStartMs = savedTracking; g_audioLastCaptureCallbackMs = savedLast;
    g_captureAudioRejected = savedRejected;
    g_playbackMuted = savedManual; g_backgroundAudioMuted = savedBackground;
    sample.Reset(); allocator->Decommit(); allocator.Reset();
    CoUninitialize();
    std::puts("Audio input recovery: boundaries, silence, warmup, retries, cancellation and real callbacks passed.");
    return 0;
}
