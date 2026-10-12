// Included by LatestVideoSampleTests: deterministic graph notifications, no
// camera, capture board, device enumeration, or registry changes.
#include <deque>

class TestGraphEvents final : public IMediaEvent {
public:
    struct Event { long code; LONG_PTR first, second; };
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::deque<Event> queue;
    unsigned freed = 0, queried = 0;
    ~TestGraphEvents() { CloseHandle(ready); }
    void Push(long code, LONG_PTR first = 0, LONG_PTR second = 0) {
        queue.push_back({code, first, second}); SetEvent(ready);
    }
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IMediaEvent && iid != IID_IDispatch) return E_NOINTERFACE;
        *object = static_cast<IMediaEvent*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --references;
        if (!remaining) delete this;
        return remaining;
    }
    STDMETHODIMP GetTypeInfoCount(UINT*) override { return E_NOTIMPL; }
    STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }
    STDMETHODIMP GetEventHandle(OAEVENT* event) override {
        *event = reinterpret_cast<OAEVENT>(ready); return S_OK;
    }
    STDMETHODIMP GetEvent(long* code, LONG_PTR* first, LONG_PTR* second, long) override {
        ++queried;
        if (queue.empty()) return E_ABORT;
        const auto event = queue.front(); queue.pop_front();
        *code = event.code; *first = event.first; *second = event.second;
        if (queue.empty()) ResetEvent(ready);
        return S_OK;
    }
    STDMETHODIMP WaitForCompletion(long, long*) override { return E_NOTIMPL; }
    STDMETHODIMP CancelDefaultHandling(long) override { return E_NOTIMPL; }
    STDMETHODIMP RestoreDefaultHandling(long) override { return E_NOTIMPL; }
    STDMETHODIMP FreeEventParams(long, LONG_PTR, LONG_PTR) override { ++freed; return S_OK; }
private:
    ULONG references = 1;
};

static void TestCaptureGraphEvents() {
    using Reason = llcv::capture::CaptureGraphEvents::WaitReason;
    auto* source = new TestGraphEvents;
    llcv::capture::CaptureGraphEvents events;
    Check(events.Wait(nullptr, 0) == E_UNEXPECTED && events.LastWaitReason() == Reason::Error,
          "uninitialized graph wait reports an error, not completion");
    Check(events.Initialize(static_cast<IMediaEvent*>(source)) == S_OK, "graph event handle acquired");
    const HANDLE frame = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check(events.Wait(frame, 0) == S_FALSE && events.LastWaitReason() == Reason::Timeout &&
          !events.Completed() && source->queried == 0,
          "ordinary no-input timeout is not device loss and performs no COM polling");
    SetEvent(frame);
    Check(events.Wait(frame, 0) == S_OK && events.LastWaitReason() == Reason::Frame && source->queried == 0,
          "normal video frame performs no graph COM polling");
    SetEvent(wake);
    SetEvent(frame);
    Check(events.Wait(frame, 0, wake) == S_OK && events.LastWaitReason() == Reason::Wake &&
          source->queried == 0, "lifecycle wake takes priority over video without graph COM polling");
    Check(events.Wait(frame, 0, wake) == S_OK && events.LastWaitReason() == Reason::Frame &&
          source->queried == 0, "lifecycle wake leaves the pending frame available");
    SetEvent(wake);
    Check(events.Wait(nullptr, 0, wake) == S_OK && events.LastWaitReason() == Reason::Wake,
          "audio-only graph can wake without a frame handle");
    Check(events.Wait(nullptr, 0, wake) == S_FALSE && events.LastWaitReason() == Reason::Timeout &&
          source->queried == 0, "audio-only timeout with lifecycle wake still does not poll COM");
    for (unsigned i = 0; i < 1000; ++i) {
        SetEvent(frame);
        Check(events.Wait(frame, 0, wake) == S_OK && events.LastWaitReason() == Reason::Frame,
              "normal frame route remains independent of power lifecycle wake");
    }
    Check(source->queried == 0, "repeated video frames never query graph events");
    source->Push(EC_VIDEO_SIZE_CHANGED);
    Check(events.Wait(frame, 0) == S_OK && events.LastWaitReason() == Reason::Notification && source->freed == 1,
          "non-fatal notification does not stop capture and frees event parameters");
    source->Push(EC_STREAM_ERROR_STILLPLAYING, E_FAIL);
    source->Push(EC_QUALITY_CHANGE);
    Check(events.Wait(nullptr, 0) == S_OK && events.LastWaitReason() == Reason::Notification &&
          !events.Completed() && source->freed == 3, "non-stopping warnings do not become EOS or capture failure");
    for (LONG_PTR available : {LONG_PTR{0}, LONG_PTR{1}}) {
        source->Push(EC_DEVICE_LOST, 0, available);
        SetEvent(frame);
        Check(events.Wait(frame, 0) == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED) &&
              events.LastWaitReason() == Reason::Error && !events.Completed(),
              "unplug/replug graph invalidation takes priority over queued video");
        Check(events.Wait(frame, 0) == S_OK, "notification wait leaves frame event for owner");
    }
    source->Push(EC_ERRORABORT, E_ACCESSDENIED);
    Check(events.Wait(nullptr, 0) == E_ACCESSDENIED, "audio-only graph propagates original abort HRESULT");
    source->Push(EC_STREAM_ERROR_STOPPED, S_OK);
    Check(events.Wait(nullptr, 0) == E_FAIL, "invalid successful abort code is converted to failure");
    for (unsigned i = 0; i < 80; ++i) source->Push(EC_CLOCK_CHANGED);
    const unsigned before = source->freed;
    Check(events.Wait(frame, 0) == S_OK && source->freed == before + 64,
          "graph notification work is bounded per wait");
    Check(events.Wait(frame, 0) == S_OK && source->freed == before + 80,
          "remaining notifications stay signaled and are drained next time");

    // Normal completion is terminal even when the next caller supplies a ready
    // frame/wake. Reinitialization starts a genuinely new playback lifetime.
    for (bool audioOnly : {false, true}) {
        source->Push(EC_COMPLETE, S_OK);
        SetEvent(frame);
        SetEvent(wake);
        const unsigned released = source->freed;
        Check(events.Wait(audioOnly ? nullptr : frame, 0, wake) == S_OK &&
              events.LastWaitReason() == Reason::Completed && events.Completed() &&
              source->freed == released + 1,
              "normal audio/video EOS wins over simultaneous lifecycle wake and video");
        const unsigned queried = source->queried;
        for (unsigned i = 0; i < 3; ++i)
            Check(events.Wait(frame, INFINITE, wake) == S_OK && events.Completed() &&
                  events.LastWaitReason() == Reason::Completed && source->queried == queried,
                  "completed graph remains terminal without waiting or polling again");
        Check(WaitForSingleObject(frame, 0) == WAIT_OBJECT_0 &&
              WaitForSingleObject(wake, 0) == WAIT_OBJECT_0,
              "EOS does not consume unrelated frame or lifecycle wake signals");
        Check(events.Initialize(static_cast<IMediaEvent*>(source)) == S_OK && !events.Completed(),
              "reinitializing graph clears completion state");
        Check(events.Wait(nullptr, 0) == S_FALSE && events.LastWaitReason() == Reason::Timeout,
              "new playback lifetime no longer returns old completion");
    }
    source->Push(EC_COMPLETE, E_ACCESSDENIED);
    Check(events.Wait(nullptr, 0) == E_ACCESSDENIED && events.LastWaitReason() == Reason::Error &&
          !events.Completed(), "failed EOS status is an error, not normal completion");
    for (bool completeFirst : {false, true}) {
        const unsigned released = source->freed;
        source->Push(completeFirst ? EC_COMPLETE : EC_ERRORABORT, completeFirst ? S_OK : E_ACCESSDENIED);
        source->Push(completeFirst ? EC_ERRORABORT : EC_COMPLETE, completeFirst ? E_ACCESSDENIED : S_OK);
        SetEvent(wake);
        Check(events.Wait(nullptr, 0, wake) == E_ACCESSDENIED && !events.Completed() &&
              events.LastWaitReason() == Reason::Error && source->freed == released + 2,
              "error wins over EOS in either queue order and frees both parameter sets");
        Check(events.Wait(nullptr, 0, wake) == S_OK && events.LastWaitReason() == Reason::Wake,
              "graph failure retains the pending lifecycle wake signal");
    }
    source->Push(EC_COMPLETE, S_OK);
    source->Push(EC_ERRORABORT, E_FAIL);
    source->Push(EC_DEVICE_LOST);
    Check(events.Wait(nullptr, 0) == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED) &&
          !events.Completed(), "device loss retains priority over normal EOS and generic failure");

    for (bool errorAfterBoundary : {false, true}) {
        const unsigned released = source->freed;
        source->Push(EC_COMPLETE, S_OK);
        for (unsigned i = 1; i < 64; ++i) source->Push(EC_CLOCK_CHANGED);
        source->Push(errorAfterBoundary ? EC_ERRORABORT : EC_CLOCK_CHANGED,
                     errorAfterBoundary ? E_ACCESSDENIED : 0);
        SetEvent(frame);
        SetEvent(wake);
        Check(events.Wait(frame, 0, wake) == S_OK && !events.Completed() &&
              events.LastWaitReason() == Reason::Notification && source->freed == released + 64,
              "EOS waits for already queued events beyond the bounded drain");
        const HRESULT result = events.Wait(frame, 0, wake);
        Check(result == (errorAfterBoundary ? E_ACCESSDENIED : S_OK) &&
              events.Completed() == !errorAfterBoundary && source->freed == released + 65 &&
              events.LastWaitReason() == (errorAfterBoundary ? Reason::Error : Reason::Completed),
              "next bounded drain preserves EOS or surfaces the later queued failure");
        ResetEvent(frame);
        ResetEvent(wake);
        Check(events.Initialize(static_cast<IMediaEvent*>(source)) == S_OK,
              "reset graph after boundary completion case");
    }
    // Exactly a full batch must not need another signal/timeout to discover
    // that the completion queue is empty.
    for (unsigned i = 0; i < 63; ++i) source->Push(EC_CLOCK_CHANGED);
    source->Push(EC_COMPLETE, S_OK);
    const unsigned boundaryQueries = source->queried;
    Check(events.Wait(nullptr, 0) == S_OK && events.Completed() &&
          events.LastWaitReason() == Reason::Completed && source->queried == boundaryQueries + 64,
          "EOS exactly on the batch boundary completes without a 65th COM call");
    events.Reset();
    Check(!events.Completed() && events.Wait(nullptr, 0) == E_UNEXPECTED &&
          events.LastWaitReason() == Reason::Error, "reset removes terminal state and borrowed graph handle");
    Check(source->queue.empty(), "all consumed graph events released across terminal and warning cases");
    source->Release();
    CloseHandle(frame);
    CloseHandle(wake);
}
