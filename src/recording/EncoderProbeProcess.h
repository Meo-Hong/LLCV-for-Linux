#pragma once
#include "recording/EncoderSupport.h"
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <string_view>

namespace llcv::recording {
struct EncoderProbeResult {
    Capabilities capabilities{};
    bool completed = false;
};

namespace probe_process {
inline constexpr wchar_t kArgument[] = L"--llcv-encoder-probe=";
inline constexpr size_t kPayloadLimit = 256 * 1024;
struct Exchange {
    volatile LONG complete;
    DWORD bytes;
    BYTE payload[kPayloadLimit];
};
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (value_) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
private:
    HANDLE value_;
};
struct Unmap { void operator()(Exchange* p) const { if (p) UnmapViewOfFile(p); } };
using View = std::unique_ptr<Exchange, Unmap>;

// A bounded, version-local result channel; no settings or temporary files.
inline bool Encode(const Capabilities& caps, Exchange& exchange) {
    size_t cursor = 0;
    auto put = [&](const void* data, size_t bytes) {
        if (bytes > kPayloadLimit - cursor) return false;
        std::memcpy(exchange.payload + cursor, data, bytes);
        cursor += bytes;
        return true;
    };
    auto number = [&](DWORD n) { return put(&n, sizeof(n)); };
    auto string = [&](const std::wstring& s) {
        return s.size() <= 4096 && number(static_cast<DWORD>(s.size())) &&
            put(s.data(), s.size() * sizeof(wchar_t));
    };
    for (const auto& c : caps) {
        const DWORD flags = (c.container ? 1u : 0u) | (c.software ? 2u : 0u) |
            (c.hardwareRates.bitrate ? 4u : 0u) | (c.hardwareRates.quality ? 8u : 0u) |
            (c.softwareRates.bitrate ? 16u : 0u) | (c.softwareRates.quality ? 32u : 0u);
        if (c.hardware.size() > 64 || !number(flags) ||
            !number(static_cast<DWORD>(c.hardware.size()))) return false;
        for (const auto& e : c.hardware) if (!string(e.name) || !string(e.id)) return false;
    }
    exchange.bytes = static_cast<DWORD>(cursor);
    InterlockedExchange(&exchange.complete, 1);
    return true;
}
inline EncoderProbeResult Decode(const Exchange& exchange) {
    EncoderProbeResult result;
    if (exchange.complete != 1 || exchange.bytes > kPayloadLimit) return result;
    size_t cursor = 0;
    auto take = [&](void* data, size_t bytes) {
        if (bytes > exchange.bytes - cursor) return false;
        std::memcpy(data, exchange.payload + cursor, bytes);
        cursor += bytes;
        return true;
    };
    auto number = [&](DWORD& n) { return take(&n, sizeof(n)); };
    auto string = [&](std::wstring& s) {
        DWORD length = 0;
        if (!number(length) || length > 4096 || length * sizeof(wchar_t) > exchange.bytes - cursor) return false;
        s.resize(length);
        return take(s.data(), length * sizeof(wchar_t));
    };
    Capabilities caps;
    for (auto& c : caps) {
        DWORD flags = 0, count = 0;
        if (!number(flags) || flags > 63 || !number(count) || count > 64) return result;
        c.container = (flags & 1) != 0; c.software = (flags & 2) != 0;
        c.hardwareRates = {(flags & 4) != 0, (flags & 8) != 0};
        c.softwareRates = {(flags & 16) != 0, (flags & 32) != 0};
        for (DWORD i = 0; i < count; ++i) {
            EncoderInfo e;
            if (!string(e.name) || !string(e.id)) return result;
            c.hardware.push_back(std::move(e));
        }
    }
    if (cursor != exchange.bytes) return result;
    result.capabilities = std::move(caps);
    result.completed = true;
    return result;
}
inline std::wstring Executable() {
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) return {};
    path.resize(size);
    return path;
}

// Only the capability query is isolated. A vendor MFT may never return from
// activation, encoding or Finalize; cancelling its thread inside the viewer
// would leave COM/GPU locks and callback lifetimes unsafe.
inline EncoderProbeResult Run(const std::atomic<bool>* stop, DWORD timeoutMs = 60000,
                              const wchar_t* fixtureArgument = L"") {
    if (stop && stop->load()) return {};
    const auto executable = Executable();
    if (executable.empty()) return {};
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE,
                                      0, sizeof(Exchange), nullptr));
    if (!mapping.get()) return {};
    View exchange(static_cast<Exchange*>(MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Exchange))));
    if (!exchange) return {};
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.get() || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return {};

    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<BYTE> attributes(attributeBytes);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes)) return {};
    struct DeleteAttributes { LPPROC_THREAD_ATTRIBUTE_LIST p; ~DeleteAttributes() { DeleteProcThreadAttributeList(p); } } cleanup{list};
    HANDLE inherited = mapping.get();
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   &inherited, sizeof(inherited), nullptr, nullptr)) return {};
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.lpAttributeList = list;
    std::wstring command = L"\"" + executable + L"\" ";
    if (*fixtureArgument) command += std::wstring(fixtureArgument) + L" ";
    command += kArgument;
    command += std::to_wstring(reinterpret_cast<uintptr_t>(mapping.get()));
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                        nullptr, nullptr, &startup.StartupInfo, &process)) return {};
    Handle child(process.hProcess), thread(process.hThread);
    // Assign before running any driver code; an app exit also kills the helper.
    if (!AssignProcessToJobObject(job.get(), child.get())) {
        TerminateProcess(child.get(), 1);
        WaitForSingleObject(child.get(), 1000);
        return {};
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) return {};
    const ULONGLONG started = GetTickCount64();
    for (;;) {
        if ((stop && stop->load()) || GetTickCount64() - started >= timeoutMs) break;
        const DWORD waited = WaitForSingleObject(child.get(), 20);
        if (waited == WAIT_OBJECT_0) {
            DWORD exitCode = 1;
            if (GetExitCodeProcess(child.get(), &exitCode) && exitCode == 0) return Decode(*exchange);
            return {};
        }
        if (waited == WAIT_FAILED) break;
    }
    TerminateJobObject(job.get(), 1);
    // Never join a driver-bound process indefinitely, even during cancellation.
    WaitForSingleObject(child.get(), 1000);
    return {};
}
} // namespace probe_process

// Called at the very start of wWinMain, before profiles, UI or capture startup.
// -1 means a normal viewer launch; malformed private requests fail closed.
inline int RunEncoderProbeChild(const wchar_t* command, Capabilities (*probe)()) {
    const std::wstring_view argument = command ? command : L"";
    constexpr std::wstring_view prefix = probe_process::kArgument;
    if (!argument.starts_with(prefix)) return -1;
    const auto digits = argument.substr(prefix.size());
    if (digits.empty()) return 2;
    uintptr_t value = 0;
    for (const wchar_t digit : digits) {
        if (digit < L'0' || digit > L'9' ||
            value > (std::numeric_limits<uintptr_t>::max() - (digit - L'0')) / 10) return 2;
        value = value * 10 + (digit - L'0');
    }
    probe_process::Handle mapping(reinterpret_cast<HANDLE>(value));
    probe_process::View exchange(static_cast<probe_process::Exchange*>(
        MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(probe_process::Exchange))));
    if (!exchange) return 2;
    // Do not let an encoder crash display a blocking system error dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try { return probe_process::Encode(probe(), *exchange) ? 0 : 3; }
    catch (...) { return 3; }
}
inline EncoderProbeResult ProbeEncodersIsolated(const std::atomic<bool>* stop) noexcept {
    // A failed allocation in the controller must not escape its std::thread.
    try { return probe_process::Run(stop); }
    catch (...) { return {}; }
}
} // namespace llcv::recording
