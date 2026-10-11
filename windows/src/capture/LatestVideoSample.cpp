#include "capture/LatestVideoSample.h"

#include <dvdmedia.h>

#include <chrono>
#include <cstdio>

namespace llcv::capture {
namespace {

template<class T>
void SafeRelease(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

}  // namespace

LatestVideoSample::LatestVideoSample(
    size_t expectedBytes, HANDLE readyEvent, VideoSampleTelemetry telemetry)
    : expectedBytes_(expectedBytes),
      readyEvent_(readyEvent),
      telemetry_(telemetry) {}

LatestVideoSample::~LatestVideoSample() {
    SafeRelease(latest_);
}

bool LatestVideoSample::TrackingActive() const {
    return telemetry_.trackingStartMilliseconds &&
        GetTickCount64() >= telemetry_.trackingStartMilliseconds->load(
            std::memory_order_acquire);
}

void LatestVideoSample::Push(IMediaSample* sample) {
    const long bytes = sample ? sample->GetActualDataLength() : 0;
    if (!sample || bytes <= 0 || bytes > sample->GetSize() ||
        static_cast<size_t>(bytes) < expectedBytes_) {
        rejectedSamples_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const int64_t arrivalMicroseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    sample->AddRef();
    IMediaSample* replaced = nullptr;
    bool rejected = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (FAILED(formatFailure_.load(std::memory_order_relaxed))) {
            rejected = true;
        } else {
            replaced = latest_;
            latest_ = sample;
            latestArrivalMicroseconds_ = arrivalMicroseconds;
        }
    }
    // A concurrent format rejection must not publish another stale frame.
    if (rejected) { sample->Release(); return; }
    if (replaced) {
        replaced->Release();
        if (TrackingActive() && telemetry_.replacedFrames) {
            telemetry_.replacedFrames->fetch_add(
                1, std::memory_order_relaxed);
        }
    }
    if (TrackingActive() && telemetry_.capturedFrames) {
        telemetry_.capturedFrames->fetch_add(1, std::memory_order_relaxed);
    }
    SetEvent(readyEvent_);
}

void LatestVideoSample::RejectFormat(HRESULT failure) {
    IMediaSample* discarded = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (SUCCEEDED(formatFailure_.load(std::memory_order_relaxed)))
            formatFailure_.store(FAILED(failure) ? failure : VFW_E_TYPE_NOT_ACCEPTED,
                                 std::memory_order_release);
        discarded = latest_;
        latest_ = nullptr;
    }
    SafeRelease(discarded);
    SetEvent(readyEvent_); // Wake the owner even if no further samples arrive.
}

IMediaSample* LatestVideoSample::TakeLatest(
    int64_t& arrivalMicroseconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    IMediaSample* sample = latest_;
    latest_ = nullptr;
    if (!sample) return nullptr;
    arrivalMicroseconds = latestArrivalMicroseconds_;
    return sample;
}

VideoSampleGrabberCallback::VideoSampleGrabberCallback(
    LatestVideoSample* sampleSlot, diagnostics::LogSink, video::VideoSampleFormat format)
    : sampleSlot_(sampleSlot), format_(format) {}

void LatestVideoSample::RefreshAfterOutputReset(IMediaSample*& current,
                                               int64_t& arrivalMicroseconds) {
    if (auto* newer = TakeLatest(arrivalMicroseconds)) {
        if (current && TrackingActive() && telemetry_.replacedFrames)
            telemetry_.replacedFrames->fetch_add(1, std::memory_order_relaxed);
        SafeRelease(current);
        current = newer;
    }
}

STDMETHODIMP VideoSampleGrabberCallback::QueryInterface(
    REFIID id, void** object) {
    if (!object) return E_POINTER;
    if (id == IID_IUnknown || id == __uuidof(ISampleGrabberCB)) {
        *object = static_cast<ISampleGrabberCB*>(this);
        AddRef();
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) VideoSampleGrabberCallback::AddRef() {
    return ++references_;
}

STDMETHODIMP_(ULONG) VideoSampleGrabberCallback::Release() {
    const ULONG value = --references_;
    if (!value) delete this;
    return value;
}

STDMETHODIMP VideoSampleGrabberCallback::SampleCB(
    double, IMediaSample* sample) {
    if (!sample) return E_POINTER;
    if (!sampleSlot_) return S_OK;
    if (FAILED(sampleSlot_->FormatFailure())) return VFW_E_TYPE_NOT_ACCEPTED;
    AM_MEDIA_TYPE* changed = nullptr;
    const HRESULT typeHr = sample->GetMediaType(&changed);
    const bool matches = (typeHr == S_FALSE && !changed) ||
        (typeHr == S_OK && changed && video::MatchesVideoSampleFormat(changed, format_));
    if (changed) {
        CoTaskMemFree(changed->pbFormat);
        if (changed->pUnk) changed->pUnk->Release();
        CoTaskMemFree(changed);
    }
    if (!matches) {
        sampleSlot_->RejectFormat(FAILED(typeHr) ? typeHr : VFW_E_TYPE_NOT_ACCEPTED);
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    // This pipeline uploads CPU samples. Probing an unused VRAM interface (and
    // logging it) before publishing can delay the first frame on some drivers.
    // GetMediaType above is the required change notification (normally S_FALSE,
    // no allocation). Keep optional COM probes and I/O off this callback.
    sampleSlot_->Push(sample);
    return S_OK;
}

STDMETHODIMP VideoSampleGrabberCallback::BufferCB(double, BYTE*, long) {
    return E_NOTIMPL;
}

}  // namespace llcv::capture
