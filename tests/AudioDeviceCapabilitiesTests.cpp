#include "audio/AudioDeviceCapabilities.h"
#include "audio/ExclusiveEventStream.h"

#include <cstdio>

struct SilentPacket final : IAudioRenderClient {
    UINT32 requested = 0, released = 0;
    DWORD flags = 0;
    HRESULT getResult = S_OK;
    STDMETHODIMP QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP GetBuffer(UINT32 frames, BYTE** bytes) override {
        requested = frames; *bytes = nullptr; return getResult;
    }
    STDMETHODIMP ReleaseBuffer(UINT32 frames, DWORD value) override {
        released = frames; flags = value; return S_OK;
    }
};

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, const char* message) {
        if (condition) return;
        std::fprintf(stderr, "FAILED: %s\n", message);
        ++failures;
    };

    const WAVEFORMATEX format = llcv::audio_device::PcmOutputFormat();
    check(format.wFormatTag == WAVE_FORMAT_PCM, "PCM output format tag");
    check(format.nSamplesPerSec == 48'000, "PCM output sample rate");
    check(format.nChannels == 2, "PCM output channel count");
    check(format.wBitsPerSample == 16, "PCM output bit depth");
    check(format.nBlockAlign == 4, "PCM output block alignment");
    check(format.nAvgBytesPerSec == 192'000,
          "PCM output average byte rate");

    llcv::audio_device::SharedModeSupport support{};
    support.supported = true;
    support.minimumFrames = 96;
    support.maximumFrames = 480;
    support.fundamentalFrames = 48;
    check(llcv::audio_device::ClosestSupportedSharedPeriod(200, support) ==
              192,
          "Shared period rounds to the nearest fundamental multiple");
    check(llcv::audio_device::ClosestSupportedSharedPeriod(1, support) == 96,
          "Shared period clamps to the endpoint minimum");
    check(llcv::audio_device::ClosestSupportedSharedPeriod(900, support) ==
              480,
          "Shared period clamps to the endpoint maximum");

    support.supported = false;
    check(llcv::audio_device::ClosestSupportedSharedPeriod(200, support) == 0,
          "Unsupported Shared endpoint returns no period");

    for (const int bufferMs : {5, 10, 15, 20, 30, 40}) {
        check(llcv::audio_device::IsExclusiveLowLatencyBuffer(bufferMs),
              "Known Exclusive buffer is accepted");
    }
    for (const int bufferMs : {0, 25, 50}) {
        check(!llcv::audio_device::IsExclusiveLowLatencyBuffer(bufferMs),
              "Unknown Exclusive buffer is rejected");
    }

    SilentPacket packet;
    check(SUCCEEDED(llcv::audio_device::SubmitExclusiveSilence(&packet, 448)) &&
          packet.requested == 448 && packet.released == 448 &&
          packet.flags == AUDCLNT_BUFFERFLAGS_SILENT,
          "probe submits a full aligned silent packet");
    packet.released = 0;
    packet.getResult = AUDCLNT_E_BUFFER_ERROR;
    check(FAILED(llcv::audio_device::SubmitExclusiveSilence(&packet, 448)) &&
          packet.released == 0, "failed acquisition never releases an unowned packet");
    llcv::audio_device::ExclusiveProbe probe;
    probe.requestedFrames = 240;
    probe.actualBufferFrames = 448;
    probe.testDurationMs = 5000;
    probe.events = 535;
    probe.submittedFrames = 535 * 448;
    probe.maximumEventMs = 10;
    using llcv::audio_device::EvaluateExclusiveTiming;
    using Result = llcv::audio_device::ExclusiveTimingResult;
    check(EvaluateExclusiveTiming(probe) == Result::Passed,
          "aligned 9.33ms device passes a 5ms request using actual cadence");
    probe.submittedFrames /= 2;
    check(EvaluateExclusiveTiming(probe) == Result::InsufficientSupply, "half-rate supply rejected");
    probe.submittedFrames *= 2;
    probe.maximumEventMs = 100;
    check(EvaluateExclusiveTiming(probe) == Result::IrregularEvents, "stalled device not approved");
    probe.maximumEventMs = 10;
    probe.testDurationMs = 4999;
    check(EvaluateExclusiveTiming(probe) == Result::InsufficientDuration, "short observation rejected");
    probe.testDurationMs = 5000;
    probe.actualBufferFrames = 1921;
    check(EvaluateExclusiveTiming(probe) == Result::InvalidBuffer, "over-40ms buffer rejected");
    probe.actualBufferFrames = 0;
    check(EvaluateExclusiveTiming(probe) == Result::InvalidBuffer, "zero buffer rejected");
    probe.actualBufferFrames = 480;
    probe.events = 450;
    probe.maximumEventMs = 13.5;
    probe.submittedFrames = 235200;
    check(EvaluateExclusiveTiming(probe) == Result::Passed, "exact timing thresholds accepted");
    --probe.submittedFrames;
    check(EvaluateExclusiveTiming(probe) == Result::InsufficientSupply, "below supply threshold rejected");
    ++probe.submittedFrames;
    --probe.events;
    check(EvaluateExclusiveTiming(probe) == Result::IrregularEvents, "below event count threshold rejected");
    ++probe.events;
    probe.maximumEventMs = 13.501;
    check(EvaluateExclusiveTiming(probe) == Result::IrregularEvents, "above stall threshold rejected");

    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed.\n", failures);
        return 1;
    }
    std::puts("Audio device capability tests passed.");
    return 0;
}
