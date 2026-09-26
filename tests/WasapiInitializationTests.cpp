// Calls the production setup helper with fake COM endpoints; no audio device is opened.
#include "../src/audio/WasapiOutput.cpp"
#include <cstdlib>

static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAILED: %s\n", message); std::abort(); }
}
struct Trace {
    HRESULT first = AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED, second = S_OK;
    HRESULT sizeResult = S_OK, activateResult = S_OK;
    UINT32 frames = 448;
    unsigned initialized[2]{}, destroyed[2]{}, activated = 0;
    REFERENCE_TIME duration[2]{};
};
class Client final : public IAudioClient {
public:
    Client(Trace& trace, unsigned index) : t(trace), id(index) {}
    STDMETHODIMP QueryInterface(REFIID, void** p) override { *p = nullptr; return E_NOINTERFACE; }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override {
        const auto n = --refs; if (!n) { ++t.destroyed[id]; delete this; } return n;
    }
    STDMETHODIMP Initialize(AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME buffer,
                            REFERENCE_TIME period, const WAVEFORMATEX* format, LPCGUID) override {
        Check(++t.initialized[id] == 1, "each audio client is initialized only once");
        Check(mode == AUDCLNT_SHAREMODE_EXCLUSIVE && flags == AUDCLNT_STREAMFLAGS_EVENTCALLBACK &&
              period == buffer && format->nSamplesPerSec == 48000, "retry preserves exclusive event format");
        t.duration[id] = buffer;
        return id ? t.second : t.first;
    }
    STDMETHODIMP GetBufferSize(UINT32* frames) override { *frames = t.frames; return t.sizeResult; }
    STDMETHODIMP GetStreamLatency(REFERENCE_TIME*) override { return E_NOTIMPL; }
    STDMETHODIMP GetCurrentPadding(UINT32*) override { return E_NOTIMPL; }
    STDMETHODIMP IsFormatSupported(AUDCLNT_SHAREMODE, const WAVEFORMATEX*, WAVEFORMATEX**) override { return E_NOTIMPL; }
    STDMETHODIMP GetMixFormat(WAVEFORMATEX**) override { return E_NOTIMPL; }
    STDMETHODIMP GetDevicePeriod(REFERENCE_TIME*, REFERENCE_TIME*) override { return E_NOTIMPL; }
    STDMETHODIMP Start() override { return E_NOTIMPL; }
    STDMETHODIMP Stop() override { return E_NOTIMPL; }
    STDMETHODIMP Reset() override { return E_NOTIMPL; }
    STDMETHODIMP SetEventHandle(HANDLE) override { return E_NOTIMPL; }
    STDMETHODIMP GetService(REFIID, void**) override { return E_NOTIMPL; }
private:
    Trace& t; unsigned id; ULONG refs = 1;
};
class Device final : public IMMDevice {
public:
    explicit Device(Trace& trace) : t(trace) {}
    STDMETHODIMP QueryInterface(REFIID, void** p) override { *p = nullptr; return E_NOINTERFACE; }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP Activate(REFIID iid, DWORD, PROPVARIANT*, void** p) override {
        *p = nullptr; ++t.activated;
        Check(iid == __uuidof(IAudioClient) && t.destroyed[0] == 1,
              "old client must be released before activating replacement");
        if (FAILED(t.activateResult)) return t.activateResult;
        *p = static_cast<IAudioClient*>(new Client(t, 1)); return S_OK;
    }
    STDMETHODIMP OpenPropertyStore(DWORD, IPropertyStore**) override { return E_NOTIMPL; }
    STDMETHODIMP GetId(LPWSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP GetState(DWORD*) override { return E_NOTIMPL; }
private:
    Trace& t;
};

int main() {
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        Trace trace;
        HRESULT expected = S_OK;
        if (scenario == 1) trace.first = S_OK;
        if (scenario == 2) expected = trace.first = AUDCLNT_E_UNSUPPORTED_FORMAT;
        if (scenario == 3) expected = trace.sizeResult = E_FAIL;
        if (scenario == 4) { trace.frames = 0; expected = E_INVALIDARG; }
        if (scenario == 5) expected = trace.activateResult = E_ACCESSDENIED;
        if (scenario == 6) expected = trace.second = AUDCLNT_E_DEVICE_INVALIDATED;
        if (scenario == 7) expected = trace.second = AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED;
        Device device(trace);
        IAudioClient* client = new Client(trace, 0);
        auto format = llcv::audio_device::PcmOutputFormat();
        const HRESULT result = llcv::wasapi::InitializeExclusiveAligned(
            &device, client, format, 100000, {});
        Check(result == expected, "setup propagates the correct success or failure");
        const bool replacement = scenario == 0 || scenario >= 5;
        Check(trace.activated == (replacement ? 1u : 0u), "only alignment errors trigger one activation");
        if (replacement && scenario != 5) {
            Check(trace.initialized[1] == 1 && trace.duration[1] == 93333,
                  "new client receives nearest 100 ns aligned frame duration");
        }
        if (client) client->Release();
        Check(trace.destroyed[0] == 1 && trace.destroyed[1] ==
              (replacement && scenario != 5 ? 1u : 0u), "all clients released exactly once");
    }
    std::puts("WASAPI exclusive initialization: 8 success/failure/lifetime cases passed.");
}
