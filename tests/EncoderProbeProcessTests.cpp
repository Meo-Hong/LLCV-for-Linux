#include "recording/EncoderProbeProcess.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace llcv::recording;
namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
Capabilities FixtureCapabilities() {
    Capabilities caps{};
    caps[0].container = true; caps[0].software = true;
    caps[0].hardwareRates = {true, true}; caps[0].softwareRates = {true, true};
    caps[0].hardware = {{L"한글 H.264 인코더", L"gpu-0/장치"}, {L"Second encoder", L"gpu-1"}};
    caps[1].container = false; caps[1].software = true;
    caps[1].hardwareRates = {true, false}; caps[1].softwareRates = {false, true};
    caps[2].container = true; caps[2].software = false;
    caps[2].hardwareRates = {false, true}; caps[2].softwareRates = {true, false};
    caps[2].hardware = {{L"AV1 \U0001F3AE", L"adapter-2/AV1"}};
    return caps;
}
bool Equal(const Capabilities& a, const Capabilities& b) {
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].container != b[i].container || a[i].software != b[i].software ||
            a[i].hardwareRates.bitrate != b[i].hardwareRates.bitrate ||
            a[i].hardwareRates.quality != b[i].hardwareRates.quality ||
            a[i].softwareRates.bitrate != b[i].softwareRates.bitrate ||
            a[i].softwareRates.quality != b[i].softwareRates.quality ||
            a[i].hardware.size() != b[i].hardware.size()) return false;
        for (size_t j = 0; j < a[i].hardware.size(); ++j)
            if (a[i].hardware[j].name != b[i].hardware[j].name ||
                a[i].hardware[j].id != b[i].hardware[j].id) return false;
    }
    return true;
}
void TestProtocol() {
    auto exchange = std::make_unique<probe_process::Exchange>();
    const auto caps = FixtureCapabilities();
    Check(probe_process::Encode(caps, *exchange), "capabilities encode");
    const auto decoded = probe_process::Decode(*exchange);
    Check(decoded.completed && Equal(caps, decoded.capabilities), "all devices, Unicode names and rate flags round-trip");
    const DWORD bytes = exchange->bytes;
    for (DWORD length = 0; length < bytes; ++length) {
        exchange->bytes = length;
        Check(!probe_process::Decode(*exchange).completed, "every truncated payload fails closed");
    }
    exchange->bytes = bytes + 1;
    Check(!probe_process::Decode(*exchange).completed, "trailing payload data rejected");
    exchange->bytes = static_cast<DWORD>(probe_process::kPayloadLimit + 1);
    Check(!probe_process::Decode(*exchange).completed, "oversized declared payload rejected");
    exchange->bytes = bytes;
    for (LONG complete : {0L, 2L, -1L}) {
        exchange->complete = complete;
        Check(!probe_process::Decode(*exchange).completed, "unpublished or invalid completion marker rejected");
    }
    auto invalidNumber = [&](size_t offset, DWORD value, const char* message) {
        Check(probe_process::Encode(caps, *exchange), "refresh protocol fixture");
        std::memcpy(exchange->payload + offset, &value, sizeof(value));
        Check(!probe_process::Decode(*exchange).completed, message);
    };
    invalidNumber(0, 64, "unknown capability flag rejected");
    invalidNumber(sizeof(DWORD), 65, "too many decoded devices rejected");
    invalidNumber(2 * sizeof(DWORD), 4097, "oversized decoded string rejected");
    invalidNumber(2 * sizeof(DWORD), 4096, "string extending past published bytes rejected");

    auto rejectEncode = [&](const Capabilities& invalid, const char* message) {
        exchange->complete = 0; exchange->bytes = 0;
        Check(!probe_process::Encode(invalid, *exchange) && !probe_process::Decode(*exchange).completed, message);
    };
    auto invalid = caps;
    invalid[0].hardware.resize(65);
    rejectEncode(invalid, "encoder cannot publish more than 64 devices");
    invalid = caps; invalid[0].hardware[0].name.assign(4097, L'x');
    rejectEncode(invalid, "encoder cannot publish a name longer than 4096 characters");
    invalid = caps; invalid[0].hardware[0].id.assign(4097, L'x');
    rejectEncode(invalid, "encoder cannot publish an ID longer than 4096 characters");
    invalid = caps;
    invalid[0].hardware.assign(64, EncoderInfo{std::wstring(4096, L'x'), std::wstring(4096, L'y')});
    rejectEncode(invalid, "total payload limit holds even when individual fields are legal");

    auto boundary = caps;
    boundary[0].hardware.assign(64, EncoderInfo{L"a", L"b"});
    boundary[2].hardware[0].name.assign(4096, L'한');
    Check(probe_process::Encode(boundary, *exchange), "exact device and string limits are accepted");
    const auto limitResult = probe_process::Decode(*exchange);
    Check(limitResult.completed && Equal(boundary, limitResult.capabilities), "boundary-sized capability set preserved");
    Check(probe_process::Encode({}, *exchange), "empty capabilities encode successfully");
    const auto empty = probe_process::Decode(*exchange);
    Check(empty.completed && Equal(empty.capabilities, {}), "successful unsupported result is distinct from a failed probe");
}

bool unexpectedCallback = false;
Capabilities UnexpectedProbe() { unexpectedCallback = true; return {}; }
void TestChildArguments() {
    Check(RunEncoderProbeChild(nullptr, UnexpectedProbe) == -1, "normal launch with no command line is untouched");
    Check(RunEncoderProbeChild(L"--ordinary-launch", UnexpectedProbe) == -1, "normal launch arguments are untouched");
    for (const auto* suffix : {L"", L"-1", L"+1", L"1x", L" 1", L"1 ", L"0"}) {
        const auto command = std::wstring(probe_process::kArgument) + suffix;
        Check(RunEncoderProbeChild(command.c_str(), UnexpectedProbe) == 2, "malformed or zero mapping handle rejected");
    }
    const auto overflow = std::wstring(probe_process::kArgument) +
        std::to_wstring((std::numeric_limits<uintptr_t>::max)()) + L"0";
    Check(RunEncoderProbeChild(overflow.c_str(), UnexpectedProbe) == 2, "mapping handle integer overflow rejected");
    Check(!unexpectedCallback, "invalid private requests never enter encoder code");
}

std::wstring fixtureMode;
std::wstring fixtureSignal;
Capabilities ChildProbe() {
    if (fixtureMode == L"--success") return FixtureCapabilities();
    if (fixtureMode == L"--empty") return {};
    if (fixtureMode == L"--incomplete") ExitProcess(0);
    if (fixtureMode == L"--crash") {
        TerminateProcess(GetCurrentProcess(), 0xC0000005u);
        ExitProcess(7);
    }
    if (fixtureMode == L"--hang") {
        probe_process::Handle mapping(OpenFileMappingW(FILE_MAP_WRITE, FALSE, (fixtureSignal + L".pid").c_str()));
        probe_process::Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, (fixtureSignal + L".ready").c_str()));
        if (!mapping.get() || !ready.get()) ExitProcess(8);
        auto pid = static_cast<DWORD*>(MapViewOfFile(mapping.get(), FILE_MAP_WRITE, 0, 0, sizeof(DWORD)));
        if (!pid) ExitProcess(9);
        *pid = GetCurrentProcessId();
        UnmapViewOfFile(pid);
        if (!SetEvent(ready.get())) ExitProcess(10);
        Sleep(INFINITE); // Simulate a driver call that does not observe cancellation.
    }
    ExitProcess(11);
}

void TestHungChild(bool cancel) {
    const auto name = L"Local\\LLCVEncoderProbeTests-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()) + (cancel ? L"-cancel" : L"-timeout");
    probe_process::Handle ready(CreateEventW(nullptr, TRUE, FALSE, (name + L".ready").c_str()));
    probe_process::Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(DWORD), (name + L".pid").c_str()));
    Check(ready.get() && mapping.get(), "create isolated hung-child handshake");
    auto pid = static_cast<const DWORD*>(MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, sizeof(DWORD)));
    Check(pid != nullptr, "map test-only child identity");
    std::atomic<bool> stop{false};
    HANDLE observedChild = nullptr;
    DWORD readyResult = WAIT_FAILED;
    ULONGLONG cancelledAt = 0;
    std::thread observer([&] {
        readyResult = WaitForSingleObject(ready.get(), 2000);
        if (readyResult == WAIT_OBJECT_0)
            observedChild = OpenProcess(SYNCHRONIZE, FALSE, *pid);
        if (cancel || readyResult != WAIT_OBJECT_0) {
            cancelledAt = GetTickCount64();
            stop.store(true);
        }
    });
    const auto started = GetTickCount64();
    const auto result = probe_process::Run(&stop, cancel ? 5000 : 750, (L"--hang=" + name).c_str());
    const auto returned = GetTickCount64();
    observer.join();
    probe_process::Handle child(observedChild);
    UnmapViewOfFile(pid);
    Check(readyResult == WAIT_OBJECT_0 && child.get(), "test reached the actual uncooperative child callback");
    Check(!result.completed, "hung probe cannot become a cache-valid result");
    Check(returned - started < 2500, "hung probe returns within bounded timeout/cancellation and cleanup");
    if (cancel) Check(returned - cancelledAt < 1500, "cancellation does not join the stuck driver indefinitely");
    Check(WaitForSingleObject(child.get(), 0) == WAIT_OBJECT_0, "timed-out or cancelled helper is terminated before returning");
}
void TestProcesses() {
    const auto success = probe_process::Run(nullptr, 5000, L"--success");
    Check(success.completed && Equal(success.capabilities, FixtureCapabilities()), "child process returns complete capability data");
    const auto empty = probe_process::Run(nullptr, 5000, L"--empty");
    Check(empty.completed && Equal(empty.capabilities, {}), "successful empty child result remains cacheable");
    Check(!probe_process::Run(nullptr, 5000, L"--incomplete").completed, "zero exit without published data is not success");
    Check(!probe_process::Run(nullptr, 5000, L"--crash").completed, "crashed encoder helper is not success");
    std::atomic<bool> cancelled{true};
    const auto started = GetTickCount64();
    Check(!probe_process::Run(&cancelled, 5000, L"--hang").completed && GetTickCount64() - started < 250,
        "already cancelled probe returns without spawning a child");
    TestHungChild(false);
    TestHungChild(true);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 3) {
        fixtureMode = argv[1];
        if (fixtureMode.starts_with(L"--hang=")) {
            fixtureSignal = fixtureMode.substr(7);
            fixtureMode = L"--hang";
        }
        return RunEncoderProbeChild(argv[2], ChildProbe);
    }
    Check(argc == 1, "unexpected test command line");
    TestProtocol();
    TestChildArguments();
    TestProcesses();
    std::puts("Encoder probe process isolation and result protocol passed.");
    return 0;
}
