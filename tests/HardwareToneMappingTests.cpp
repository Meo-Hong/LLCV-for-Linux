#include "capture/HardwareToneMapping.h"
#include <dshow.h>
#include <ks.h>
#include <ksproxy.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using llcv::capture::ConfigureHardwareToneMapping;
using llcv::capture::ToneMappingStatus;
using llcv::capture::ToneMappingVendor;
using Format = llcv::settings::VideoPixelFormat;

static void Check(bool ok, const char* reason) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", reason); std::exit(1); }
}
static std::wstring lastLog;
static int logCount = 0;
static void Log(const wchar_t* text) { lastLog = text; ++logCount; }

// Does not enumerate/open any real capture device. The fixture intentionally
// implements only IUnknown/IKsPropertySet, not an actual running filter.
class FakeProperties final : public IKsPropertySet {
public:
    ULONG refs = 1;
    bool avermedia = false;
    explicit FakeProperties(bool aver = false) : avermedia(aver) {}
    HRESULT interfaceResult = S_OK;
    HRESULT queryResult = S_OK, setResult = S_OK;
    DWORD support = KSPROPERTY_SUPPORT_SET;
    bool exposeInterface = true;
    bool nullInterface = false;
    unsigned qiCalls = 0, queryCalls = 0, getCalls = 0, setCalls = 0;
    std::vector<DWORD> commands;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        ++qiCalls;
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || (exposeInterface && id == __uuidof(IKsPropertySet))) {
            if (!nullInterface) { *out = static_cast<IKsPropertySet*>(this); AddRef(); }
            return interfaceResult;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    void VerifyProperty(REFGUID set, DWORD id) const {
        const GUID aver = {0x8a80d56f, 0xfac5, 0x4692,
            {0xa4, 0x16, 0xcf, 0x20, 0xd4, 0xa1, 0x8f, 0x47}};
        const GUID elgato = {0xd1e5209f, 0x68fd, 0x4529,
            {0xbe, 0xe0, 0x5e, 0x7a, 0x1f, 0x47, 0x92, 0x26}};
        Check(set == (avermedia ? aver : elgato) && id == (avermedia ? 2ul : 722ul),
            "exact vendor GUID and property ID, no cross-vendor writes");
    }
    HRESULT STDMETHODCALLTYPE QuerySupported(REFGUID set, DWORD id, DWORD* flags) override {
        if (id != (avermedia ? 2ul : 722ul)) { *flags=0; return E_PROP_SET_UNSUPPORTED; }
        ++queryCalls; VerifyProperty(set, id);
        *flags = support; // Deliberately also write flags on failure.
        return queryResult;
    }
    HRESULT STDMETHODCALLTYPE Set(REFGUID set, DWORD id, LPVOID instance,
        DWORD instanceBytes, LPVOID data, DWORD bytes) override {
        ++setCalls; VerifyProperty(set, id);
        Check(queryCalls == setCalls, "every SET preceded by one capability query");
        if (!avermedia) {
            Check(!instance && !instanceBytes && data && bytes == sizeof(DWORD),
                "Elgato ABI: no instance and 32-bit value");
            DWORD value = 9;
            std::memcpy(&value, data, sizeof(value));
            Check(value <= 1, "Elgato payload is 0 or 1");
            commands.push_back(value);
            return setResult;
        }
        Check(instance && data && instanceBytes == 8 && bytes == 32, "vendor native x64 ABI sizes including padding");
        const std::array<unsigned char, 24> zeros{};
        Check(std::memcmp(data, zeros.data(), zeros.size()) == 0, "zero initialized vendor header");
        Check(std::memcmp(static_cast<unsigned char*>(data) + 28, zeros.data(), 4) == 0 &&
            std::memcmp(static_cast<unsigned char*>(instance) + 4, zeros.data(), 4) == 0,
            "zero tail padding in data and instance");
        DWORD value = 9, instanceValue = 9;
        std::memcpy(&value, static_cast<unsigned char*>(data) + 24, sizeof(value));
        std::memcpy(&instanceValue, instance, sizeof(instanceValue));
        Check(value <= 1 && value == instanceValue, "consistent enable value in both buffers");
        commands.push_back(value);
        return setResult;
    }
    HRESULT STDMETHODCALLTYPE Get(REFGUID, DWORD, LPVOID, DWORD, LPVOID, DWORD, DWORD*) override {
        ++getCalls; return E_NOTIMPL;
    }
};

static void TestVendor(const wchar_t* name, bool avermedia) {
    FakeProperties device(avermedia);
    auto result = ConfigureHardwareToneMapping(&device, name, Format::P010, Log);
    Check(result.status == ToneMappingStatus::Applied && !result.enable, "P010 disables hardware tonemapping");
    Check(result.vendor == (avermedia ? ToneMappingVendor::AverMedia : ToneMappingVendor::Elgato),
        "selected protocol recorded");
    Check(device.commands == std::vector<DWORD>{0}, "P010 sends one OFF");
    Check(device.refs == 1 && !device.getCalls, "COM released; no guessed GET protocol");
    Check(lastLog.find(L"requested=off") != std::wstring::npos &&
        lastLog.find(L"input encoding not verified") != std::wstring::npos, "honest success diagnostic");

    // Reopening a graph reasserts policy, including when other apps may have
    // changed the hardware meanwhile. There is no process-lifetime cache.
    for (auto format : {Format::Nv12, Format::P010, Format::Yuy2, Format::Mjpeg, Format::P010}) {
        result = ConfigureHardwareToneMapping(&device, name, format, Log);
        Check(result.status == ToneMappingStatus::Applied, "HDR/SDR sequence succeeds");
    }
    Check(device.commands == std::vector<DWORD>({0,1,0,1,1,0}), "SDR enables, P010 disables on every start");
    Check(device.setCalls == 6 && device.queryCalls == 6 && device.refs == 1, "one write per graph start");
    const auto count = device.qiCalls;
    const auto logs = logCount;
    for (auto format : {Format::Auto, static_cast<Format>(999)})
        Check(ConfigureHardwareToneMapping(&device, name, format, Log).status ==
            ToneMappingStatus::NotApplicable, "unknown negotiated format untouched");
    Check(device.qiCalls == count && logCount == logs, "no calls/log spam for unrelated paths");

    FakeProperties unsupported(avermedia);
    for (auto hr : {E_NOTIMPL, E_PROP_ID_UNSUPPORTED, E_PROP_SET_UNSUPPORTED}) {
        unsupported.queryResult = hr;
        result = ConfigureHardwareToneMapping(&unsupported, name, Format::P010, Log);
        Check(result.status == ToneMappingStatus::Unsupported && result.result == hr, "unsupported query preserved");
    }
    unsupported.queryResult = E_ACCESSDENIED;
    result = ConfigureHardwareToneMapping(&unsupported, name, Format::Nv12, Log);
    Check(result.status == ToneMappingStatus::QueryFailed && result.result == E_ACCESSDENIED, "query failure distinguished");
    unsupported.queryResult = S_FALSE;
    Check(ConfigureHardwareToneMapping(&unsupported, name, Format::P010).status ==
        ToneMappingStatus::QueryFailed, "non-S_OK capability result does not permit writes");
    unsupported.queryResult = S_OK;
    for (DWORD flags : {0ul, static_cast<DWORD>(KSPROPERTY_SUPPORT_GET), 0x80000000ul}) {
        unsupported.support = flags;
        Check(ConfigureHardwareToneMapping(&unsupported, name, Format::P010, Log).status ==
            ToneMappingStatus::Unsupported, "GET-only/no flags does not permit write");
    }
    Check(!unsupported.setCalls && unsupported.refs == 1, "no speculative vendor write after failed capability check");
    Check(lastLog.find(L"skipped") != std::wstring::npos, "unsupported reported, not success");

    FakeProperties failing(avermedia);
    for (auto hr : {E_FAIL, E_ACCESSDENIED, S_FALSE}) {
        failing.setResult = hr;
        result = ConfigureHardwareToneMapping(&failing, name, Format::P010, Log);
        Check(result.status == ToneMappingStatus::SetFailed && result.result == hr, "rejected/non-S_OK SET not reported as success");
    }
    Check(failing.setCalls == 3 && failing.refs == 1, "no automatic retries or alternate writes");
    Check(lastLog.find(L"failed") != std::wstring::npos, "failed SET diagnostic");

    FakeProperties noInterface(avermedia);
    noInterface.exposeInterface = false;
    Check(ConfigureHardwareToneMapping(&noInterface, name, Format::P010, Log).status ==
        ToneMappingStatus::NoInterface, "missing property interface safe");
    Check(!noInterface.queryCalls && !noInterface.setCalls && noInterface.refs == 1, "no operations without interface");
    Check(ConfigureHardwareToneMapping(nullptr, name, Format::P010, Log).result == E_POINTER, "null filter safe");
    noInterface.exposeInterface = true;
    noInterface.nullInterface = true;
    Check(ConfigureHardwareToneMapping(&noInterface, name, Format::P010).status ==
        ToneMappingStatus::NoInterface, "S_OK with null interface safe");
    noInterface.nullInterface = false;
    for (auto hr : {S_FALSE, E_FAIL}) {
        noInterface.interfaceResult = hr;
        Check(ConfigureHardwareToneMapping(&noInterface, name, Format::P010).status ==
            ToneMappingStatus::QueryFailed, "non-S_OK QI does not permit calls or fallback writes");
        Check(noInterface.refs == 1 && !noInterface.queryCalls && !noInterface.setCalls,
            "stray interface pointer released without driver access");
    }

    // Exercise repeated graph lifecycle decisions with no actual GPU/audio IO.
    FakeProperties repeated(avermedia);
    FakeProperties unselected(avermedia);
    for (int i = 0; i < 10000; ++i) {
        const bool sdr = i % 2 != 0;
        result = ConfigureHardwareToneMapping(&repeated, name, sdr ? Format::Nv12 : Format::P010);
        Check(result.status == ToneMappingStatus::Applied && result.enable == sdr && repeated.refs == 1,
            "repeated policy decision/refcount stable");
    }
    Check(repeated.setCalls == 10000 && repeated.getCalls == 0, "bounded one write per invocation");
    Check(!unselected.qiCalls && !unselected.setCalls, "unselected device untouched");
}

int main() {
    TestVendor(L"Elgato Game Capture 4K60 Pro MK.2", false);
    // The property capability, not a maintained product-name allowlist,
    // determines support. This is a fake, not a real 4K Pro compatibility test.
    TestVendor(L"Game Capture 4K Pro", false);
    TestVendor(L"AVerMedia HD Capture GC573 1", true);
    TestVendor(L"avermedia GC573", true);
    FakeProperties unknown;
    unknown.queryResult = E_PROP_SET_UNSUPPORTED;
    for (auto name : {L"ezcap GameDock Extreme Pro", L"Elgato 4K X", L"OBS Virtual Camera", L""}) {
        Check(ConfigureHardwareToneMapping(&unknown, name, Format::P010, Log).status ==
            ToneMappingStatus::Unsupported, "no unsupported vendor SET or speculative HID fallback");
    }
    Check(unknown.qiCalls <= 16 && unknown.queryCalls == 4 && !unknown.setCalls && unknown.refs == 1,
        "unsupported cards only receive a bounded property-support query");

    FakeProperties combinedFlags;
    combinedFlags.support = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    Check(ConfigureHardwareToneMapping(&combinedFlags, L"renamed device", Format::P010).status ==
        ToneMappingStatus::Applied && combinedFlags.setCalls == 1, "GET+SET capability supported without name table");
    FakeProperties renamedAver(true);
    Check(ConfigureHardwareToneMapping(&renamedAver, L"renamed capture card", Format::P010).status ==
        ToneMappingStatus::Applied && renamedAver.setCalls == 1, "vendor name only affects probe order");
    std::puts("HardwareToneMappingTests passed (no hardware opened).");
}
