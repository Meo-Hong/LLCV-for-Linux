#include "settings/SettingsStore.h"

#include <windows.h>
#include <sddl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

bool Check(bool condition, const char* message) {
    if (condition) return true;
    std::fprintf(stderr, "FAILED: %s\n", message);
    ++failures;
    return false;
}

bool CheckWin32(BOOL condition, const char* message) {
    if (condition) return true;
    std::fprintf(stderr, "FAILED: %s (Win32=%lu)\n", message, GetLastError());
    ++failures;
    return false;
}

bool CheckSaved(const std::wstring& path, const llcv::settings::AppSettings& settings,
                const char* message) {
    unsigned long error = ERROR_SUCCESS;
    if (llcv::settings::SaveToIni(path, settings, &error) && error == ERROR_SUCCESS)
        return true;
    std::fprintf(stderr, "FAILED: %s (save error=%lu)\n", message, error);
    ++failures;
    return false;
}

void CheckExclusiveCacheRoundTrip(const llcv::settings::AppSettings& loaded) {
    // A failed save/load must report an assertion, never dereference an empty
    // vector. Keep the count check separate so the failure stays diagnostic.
    if (!Check(loaded.exclusiveEndpointCache.size() == 3,
               "exclusive cache count round trip")) return;
    Check(loaded.exclusiveEndpointCache.back().inconclusive &&
          !loaded.exclusiveEndpointCache.back().supported,
          "temporary failure persists as explicit retry, not unsupported");
}

std::wstring TemporaryIniPath() {
    wchar_t directory[MAX_PATH]{};
    wchar_t path[MAX_PATH]{};
    if (!GetTempPathW(ARRAYSIZE(directory), directory) ||
        !GetTempFileNameW(directory, L"lcv", 0, path)) {
        return {};
    }
    return path;
}

void TestDefaults(const std::wstring& path) {
    DeleteFileW(path.c_str());
    const auto loaded = llcv::settings::LoadFromIni(path);
    Check(!loaded.settings.settingsLightTheme, "legacy and fresh profiles retain dark settings");
    Check(!loaded.settings.screenshotClipboard, "screenshot clipboard is opt-in");
    Check(!loaded.settings.capture40Fps, "40fps is opt-in for fresh and legacy profiles");
    Check(!loaded.settings.vsrEnabled, "VSR defaults OFF for fresh/legacy settings");
    Check(!loaded.settings.consoleSurround51, "5.1 is opt-in for old and fresh profiles");
    Check(loaded.settings.audioOnlyWidth == 380 && loaded.settings.audioOnlyHeight == 230,
          "audio-only default size preserves old profiles");
    Check(loaded.settings.preferredDisplayMonitor.empty(), "default display is automatic");
    Check(loaded.settings.audioMode ==
              llcv::settings::AudioMode::WasapiShared,
          "default audio mode");
    Check(loaded.settings.wasapiBufferMs == 20,
          "default WASAPI buffer");
    Check(loaded.settings.pcmQueueTargetMs == 25,
          "default PCM queue");
    Check(loaded.settings.videoPreset ==
              llcv::settings::VideoPreset::R1920x1080,
          "default resolution");
    Check(loaded.settings.checkForUpdates,
          "automatic update check defaults on");
    Check(loaded.settings.preventSleep, "sleep prevention defaults on for old/new profiles");
    Check(loaded.settings.roundedCorners, "rounded corners default on for old/new profiles");
    Check(loaded.settings.hdrChromaLocation == llcv::hdr::ChromaLocation::Auto,
          "old and fresh profiles keep automatic HDR chroma");
    WritePrivateProfileStringW(L"General", L"CheckForUpdates", L"0", path.c_str());
    Check(llcv::settings::LoadFromIni(path).settings.preventSleep,
          "legacy profile without the new key defaults to sleep prevention");
    DeleteFileW(path.c_str()); // Preserve the following new-profile test's precondition.
}

void TestNewUnicodeProfile(const std::wstring& path) {
    using namespace llcv::settings;
    // TestDefaults removed only this test's unique GetTempFileName result.
    // Exercise a genuinely new profile, not an existing empty ANSI file.
    const bool missing = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetLastError() == ERROR_FILE_NOT_FOUND;
    Check(missing, "Unicode test starts with a nonexistent temporary profile");
    if (!missing) return;
    AppSettings settings;
    settings.asioDriverName = L"ASIO 한글 드라이버 Ω";
    settings.captureDeviceId = L"캡처 장치 日本語";
    settings.captureAudioDeviceId = L"캡처 오디오 장치 Ω";
    settings.audioOutputDeviceId = L"출력 장치 日本語";
    unsigned long error = 0;
    const bool saved = SaveToIni(path, settings, &error);
    Check(saved && error == ERROR_SUCCESS, "new Unicode profile saves successfully");
    if (!saved) return;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (CheckWin32(file != INVALID_HANDLE_VALUE, "open newly saved profile for BOM check")) {
        BYTE bom[2]{};
        DWORD bytes = 0;
        if (CheckWin32(ReadFile(file, bom, sizeof(bom), &bytes, nullptr), "read profile BOM"))
            Check(bytes == sizeof(bom) && bom[0] == 0xff && bom[1] == 0xfe,
                  "a new profile is UTF-16 little endian");
        CloseHandle(file);
    }
    for (int pass = 0; pass < 2; ++pass) {
        const auto loaded = LoadFromIni(path).settings;
        Check(loaded.asioDriverName == settings.asioDriverName &&
              loaded.captureDeviceId == settings.captureDeviceId &&
              loaded.captureAudioDeviceId == settings.captureAudioDeviceId &&
              loaded.audioOutputDeviceId == settings.audioOutputDeviceId,
              "non-ASCII driver and device names survive initial save and replacement");
        if (pass == 0) {
            settings.volumePercent = 90;
            if (!CheckSaved(path, settings, "existing Unicode profile can be replaced")) return;
        }
    }
}

std::wstring CurrentUserSid() {
    HANDLE token = nullptr;
    if (!CheckWin32(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token),
                    "query current user token for temporary-file DACL")) return {};
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    if (!bytes) {
        CheckWin32(FALSE, "size current user SID");
        CloseHandle(token);
        return {};
    }
    std::vector<BYTE> storage(bytes);
    const BOOL queried = GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes);
    if (!CheckWin32(queried, "read current user SID")) {
        CloseHandle(token);
        return {};
    }
    CloseHandle(token);
    LPWSTR text = nullptr;
    if (!CheckWin32(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid,
                                         &text), "format current user SID")) return {};
    const std::wstring sid(text);
    LocalFree(text);
    return sid;
}

std::wstring ReadDacl(const std::wstring& path) {
    DWORD bytes = 0;
    GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &bytes);
    if (!bytes) {
        CheckWin32(FALSE, "size temporary-profile DACL");
        return {};
    }
    std::vector<BYTE> storage(bytes);
    if (!CheckWin32(GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION,
        storage.data(), bytes, &bytes), "read temporary-profile DACL")) return {};
    LPWSTR text = nullptr;
    if (!CheckWin32(ConvertSecurityDescriptorToStringSecurityDescriptorW(storage.data(),
        SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &text, nullptr),
        "format temporary-profile DACL")) return {};
    const std::wstring dacl(text);
    LocalFree(text);
    return dacl;
}

void TestDaclPreservation(const std::wstring& path) {
    const auto sid = CurrentUserSid();
    if (sid.empty()) return;
    // Only the unique temporary INI is changed. Keep full access for its current
    // user, SYSTEM, and administrators; explicit protection makes ACL loss visible.
    const std::wstring policy = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + sid + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!CheckWin32(ConvertStringSecurityDescriptorToSecurityDescriptorW(policy.c_str(),
        SDDL_REVISION_1, &descriptor, nullptr), "create custom temporary-file DACL")) return;
    const BOOL applied = SetFileSecurityW(path.c_str(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
    const bool ok = CheckWin32(applied, "apply custom DACL only to the temporary profile");
    LocalFree(descriptor);
    if (!ok) return;
    const auto before = ReadDacl(path);
    if (before.empty()) return;
    Check(before.find(L"D:P") == 0, "test DACL has inheritance disabled");
    auto settings = llcv::settings::LoadFromIni(path).settings;
    settings.volumePercent = 65;
    if (!CheckSaved(path, settings, "save with a custom DACL succeeds")) return;
    const auto after = ReadDacl(path);
    Check(after == before, "replacement preserves explicit ACEs and DACL protection");
    if (after != before) {
        std::fwprintf(stderr, L"DACL before: %ls\nDACL after: %ls\n", before.c_str(), after.c_str());
    }
    Check(llcv::settings::LoadFromIni(path).settings.volumePercent == 65,
          "DACL-preserving save also updates settings");
}

void TestResolutionRoundTrips(const std::wstring& path) {
    using namespace llcv::settings;
    const struct { VideoPreset preset; const wchar_t* text; } cases[] = {
        {VideoPreset::R1280x720, L"1280x720"},
        {VideoPreset::R1920x1080, L"1920x1080"},
        {VideoPreset::R2560x1440, L"2560x1440"},
        {VideoPreset::R3840x2160, L"3840x2160"},
    };
    for (const auto& item : cases) {
        for (int fps : {0, 60, 120}) {
            AppSettings saved{};
            saved.videoPreset = item.preset;
            saved.vsrCapturePreset = item.preset;
            saved.videoFrameRate = fps;
            saved.pixelFormat = VideoPixelFormat::Nv12;
            if (!CheckSaved(path, saved, "save resolution fixture")) return;
            wchar_t value[32]{};
            GetPrivateProfileStringW(L"Video", L"Resolution", L"", value,
                                     ARRAYSIZE(value), path.c_str());
            Check(std::wstring(value) == item.text, "exact resolution persisted");
            const auto loaded = LoadFromIni(path).settings;
            Check(loaded.videoPreset == item.preset &&
                  loaded.vsrCapturePreset == item.preset &&
                  loaded.videoFrameRate == fps &&
                  loaded.pixelFormat == VideoPixelFormat::Nv12,
                  "all resolutions retain FPS and NV12 through save/load");
        }
        WritePrivateProfileStringW(L"Video", L"VsrCaptureResolution", nullptr, path.c_str());
        Check(LoadFromIni(path).settings.vsrCapturePreset == item.preset,
              "legacy profiles keep their existing capture size");
        WritePrivateProfileStringW(L"Video", L"VsrCaptureResolution", L"invalid", path.c_str());
        Check(LoadFromIni(path).settings.vsrCapturePreset == item.preset,
              "invalid VSR size falls back to existing resolution");
    }
}

void TestRoundTrip(const std::wstring& path) {
    using namespace llcv::settings;
    AppSettings saved{};
    saved.consoleSurround51 = true;
    saved.vsrEnabled = true;
    saved.vsrCapturePreset = VideoPreset::R1280x720;
    saved.preferredDisplayMonitor = L"interface:monitor-test-id";
    saved.uiLanguage = UiLanguage::English;
    saved.settingsLightTheme = true;
    saved.audioMode = AudioMode::Asio;
    saved.wasapiBufferMs = 30;
    saved.wasapiSharedPeriodFrames = 144;
    saved.driftCorrection = DriftCorrectionMode::Resample;
    saved.pcmQueueTargetMs = 25;
    saved.volumePercent = 175;
    saved.leftVolumePercent = 80;
    saved.rightVolumePercent = 65;
    saved.allowVolumeBoost = true;
    saved.volumeHudPosition = VolumeHudPosition::BottomRight;
    saved.muteWhenBackground = true;
    saved.audioOutputDeviceId = L"output-id";
    saved.exclusiveVerifiedEndpointId = L"exclusive-id";
    saved.exclusiveVerifiedBufferMs = 30;
    saved.exclusiveEndpointCache = {
        {L"supported-id", true, 20},
        {L"unsupported-id", false, 0},
        {L"busy-id", false, 0, true},
    };
    saved.asioDriverName = L"ASIO Test Driver";
    saved.videoPreset = VideoPreset::R3840x2160;
    saved.pixelFormat = VideoPixelFormat::P010;
    saved.videoFrameRate = 60;
    saved.captureDeviceId = L"capture-id";
    saved.captureAudioDeviceId = L"capture-audio-id";
    saved.presentationMode = PresentationMode::VSync;
    saved.scalingMode = ScalingMode::Sharp;
    saved.fullscreenCursorMode = FullscreenCursorMode::AlwaysVisible;
    saved.forceHdr10 = true;
    saved.hdrChromaLocation = llcv::hdr::ChromaLocation::TopLeft;
    saved.mjpegColorOverride = llcv::video_color::Override::Bt709Full;
    saved.pixelPerfect = false;
    saved.relativeWindowSize = true;
    saved.relativeWindowScalePpm = 666'667;
    saved.borderlessWindow = true;
    saved.roundedCorners = false;
    saved.windowSnap = false;
    saved.saveLog = true;
    saved.screenshotClipboard = true;
    saved.capture40Fps = true;
    saved.showDiagnosticConsole = true;
    saved.skipStartupSettings = true;
    saved.checkForUpdates = false;
    saved.preventSleep = false;
    saved.audioOnly = true;
    saved.audioOnlyWidth = 640;
    saved.audioOnlyHeight = 360;

    if (!CheckSaved(path, saved, "save complete round-trip fixture")) return;
    const LoadResult result = LoadFromIni(path);
    Check(result.settings.consoleSurround51, "surround preference round trip");
    const AppSettings& loaded = result.settings;
    Check(!loaded.roundedCorners, "rounded corners OFF persists");
    Check(loaded.audioOnlyWidth == 640 && loaded.audioOnlyHeight == 360,
          "audio-only size round trip independent from video scale");
    Check(loaded.hdrChromaLocation == saved.hdrChromaLocation, "HDR chroma round trip");
    Check(loaded.preferredDisplayMonitor == saved.preferredDisplayMonitor, "display monitor round trip");
    Check(loaded.uiLanguage == saved.uiLanguage, "language round trip");
    Check(loaded.settingsLightTheme, "light settings theme round trip");
    Check(loaded.audioMode == saved.audioMode, "audio mode round trip");
    Check(loaded.wasapiBufferMs == saved.wasapiBufferMs,
          "audio buffer round trip");
    Check(loaded.wasapiSharedPeriodFrames == saved.wasapiSharedPeriodFrames,
          "shared period round trip");
    Check(loaded.driftCorrection == saved.driftCorrection,
          "drift correction round trip");
    Check(loaded.pcmQueueTargetMs == saved.pcmQueueTargetMs,
          "PCM queue round trip");
    Check(loaded.volumePercent == saved.volumePercent,
          "master volume round trip");
    Check(loaded.leftVolumePercent == saved.leftVolumePercent &&
              loaded.rightVolumePercent == saved.rightVolumePercent,
          "channel volume round trip");
    Check(loaded.volumeHudPosition == saved.volumeHudPosition,
          "HUD position round trip");
    CheckExclusiveCacheRoundTrip(loaded);
    Check(loaded.asioDriverName == saved.asioDriverName,
          "ASIO driver round trip");
    Check(loaded.vsrEnabled == saved.vsrEnabled, "VSR preference saved/restored");
    Check(loaded.vsrCapturePreset == saved.vsrCapturePreset, "VSR capture resolution saved separately");
    Check(loaded.videoPreset == saved.videoPreset,
          "resolution round trip");
    Check(loaded.pixelFormat == saved.pixelFormat,
          "pixel format round trip");
    Check(loaded.presentationMode == saved.presentationMode,
          "presentation mode round trip");
    Check(loaded.scalingMode == saved.scalingMode,
          "scaling mode round trip");
    Check(loaded.fullscreenCursorMode == saved.fullscreenCursorMode,
          "cursor mode round trip");
    Check(loaded.mjpegColorOverride == saved.mjpegColorOverride,
          "MJPEG color override round trip");
    Check(loaded.relativeWindowScalePpm == saved.relativeWindowScalePpm,
          "relative scale round trip");
    Check(result.relativeWindowScaleVersion == kRelativeWindowScaleVersion,
          "relative scale version round trip");
    Check(loaded.saveLog && loaded.showDiagnosticConsole,
          "diagnostics round trip");
    Check(loaded.screenshotClipboard, "screenshot clipboard round trip");
    Check(loaded.capture40Fps, "40fps opt-in round trip");
    Check(loaded.skipStartupSettings && !loaded.checkForUpdates &&
              loaded.audioOnly && !loaded.preventSleep,
          "general settings round trip");
}

void TestPresentationModes(const std::wstring& path) {
    using namespace llcv::settings;
    for (const auto mode : {PresentationMode::AllowTearing,
                            PresentationMode::VSync,
                            PresentationMode::Compatibility}) {
        AppSettings settings{};
        settings.presentationMode = mode;
        if (!CheckSaved(path, settings, "save presentation-mode fixture")) return;
        Check(LoadFromIni(path).settings.presentationMode == mode,
              "all presentation modes round trip");
        Check(LoadFromIni(path).settings.preventSleep, "enabled sleep preference round trips");
    }
    WritePrivateProfileStringW(L"Video", L"Presentation", L"unknown", path.c_str());
    Check(LoadFromIni(path).settings.presentationMode == PresentationMode::AllowTearing,
          "unknown presentation mode retains original default");
    WritePrivateProfileStringW(L"Video", L"Presentation", nullptr, path.c_str());
    Check(LoadFromIni(path).settings.presentationMode == PresentationMode::AllowTearing,
          "missing presentation mode retains original default");
}

void TestPcmDefaultAndPreservation(const std::wstring& path) {
    using namespace llcv::settings;
    Check(AppSettings{}.pcmQueueTargetMs == 25, "fresh/reset settings use 25ms");
    for (int value : {10, 15, 20, 25, 30}) {
        AppSettings saved{};
        saved.pcmQueueTargetMs = value;
        if (!CheckSaved(path, saved, "save PCM default fixture")) return;
        Check(LoadFromIni(path).settings.pcmQueueTargetMs == value,
              "existing saved PCM values must not migrate to the new default");
    }
    Check(WritePrivateProfileStringW(L"Audio", L"PcmQueueTargetMs", nullptr, path.c_str()) != 0,
          "remove PCM setting from temporary test INI");
    Check(LoadFromIni(path).settings.pcmQueueTargetMs == 25,
          "missing PCM key uses the same default as fresh settings");
    Check(WritePrivateProfileStringW(L"Audio", L"PcmQueueTargetMs", L"999", path.c_str()) != 0,
          "write invalid PCM setting to temporary test INI");
    Check(LoadFromIni(path).settings.pcmQueueTargetMs == 25,
          "invalid PCM value falls back to the current default");
}

void TestLegacyPcmMigration(const std::wstring& path) {
    using namespace llcv::settings;
    for (int value : {10, 15, 20, 25, 30}) {
        AppSettings saved{};
        saved.pcmQueueTargetMs = value;
        saved.skipStartupSettings = true;
        saved.volumePercent = 75;
        if (!CheckSaved(path, saved, "save legacy PCM migration fixture")) return;
        Check(WritePrivateProfileStringW(L"Audio", L"PcmQueueDefaultsVersion", nullptr, path.c_str()) != 0,
              "create legacy unversioned PCM settings");
        Check(WritePrivateProfileStringW(L"Custom", L"Untouched", L"73", path.c_str()) != 0,
              "create unrelated test setting");
        const int expected = value == 20 ? 25 : value;
        Check(LoadFromIni(path).settings.pcmQueueTargetMs == expected,
              "read-only loading applies migration in memory too");
        Check(GetPrivateProfileIntW(L"Audio", L"PcmQueueTargetMs", 0, path.c_str()) == static_cast<UINT>(value),
              "LoadFromIni remains read-only");
        Check(MigrateLegacyPcmQueueTarget(path), "legacy migration persists");
        Check(GetPrivateProfileIntW(L"Audio", L"PcmQueueTargetMs", 0, path.c_str()) == static_cast<UINT>(expected),
              "only legacy 20ms is upgraded on disk");
        Check(MigrateLegacyPcmQueueTarget(path), "migration is repeatable without changing the result");
        const auto loaded = LoadFromIni(path).settings;
        Check(loaded.pcmQueueTargetMs == expected && loaded.skipStartupSettings && loaded.volumePercent == 75 &&
              GetPrivateProfileIntW(L"Custom", L"Untouched", 0, path.c_str()) == 73,
              "direct-start and unrelated settings are preserved");
    }
    AppSettings chosen{};
    chosen.pcmQueueTargetMs = 20;
    if (!CheckSaved(path, chosen, "save user-selected PCM value")) return;
    Check(MigrateLegacyPcmQueueTarget(path) && LoadFromIni(path).settings.pcmQueueTargetMs == 20,
          "user may select 20ms again after migration");
    Check(WritePrivateProfileStringW(L"Audio", L"PcmQueueDefaultsVersion", L"2", path.c_str()) != 0,
          "write future migration marker");
    Check(MigrateLegacyPcmQueueTarget(path) && LoadFromIni(path).settings.pcmQueueTargetMs == 20,
          "future marker must not trigger old migration");
    DeleteFileW(path.c_str());
    Check(MigrateLegacyPcmQueueTarget(path) && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES,
          "migration must not create a settings file for a fresh install");
}

void TestHdrChroma(const std::wstring& path) {
    using namespace llcv;
    for (auto mode : {hdr::ChromaLocation::Auto, hdr::ChromaLocation::TopLeft, hdr::ChromaLocation::Left}) {
        settings::AppSettings saved{};
        saved.pixelFormat = settings::VideoPixelFormat::P010;
        saved.hdrChromaLocation = mode;
        if (!CheckSaved(path, saved, "save HDR placement fixture")) return;
        Check(settings::LoadFromIni(path).settings.hdrChromaLocation == mode,
              "each HDR placement survives save/load");
    }
    for (const wchar_t* value : {L"garbage", L"6", L"-1", L"999", L""}) {
        WritePrivateProfileStringW(L"Video", L"HdrChromaLocation", value, path.c_str());
        Check(settings::LoadFromIni(path).settings.hdrChromaLocation == hdr::ChromaLocation::Auto,
              "invalid HDR placement never enables an override");
    }
    WritePrivateProfileStringW(L"Video", L"HdrChromaLocation", L"topleft", path.c_str());
    Check(settings::LoadFromIni(path).settings.hdrChromaLocation == hdr::ChromaLocation::TopLeft,
          "placement string is case insensitive");
    WritePrivateProfileStringW(L"Video", L"HdrChromaLocation", nullptr, path.c_str());
    Check(settings::LoadFromIni(path).settings.hdrChromaLocation == hdr::ChromaLocation::Auto,
          "missing placement from legacy profile stays Auto");
}

void TestSaveFailures(const std::wstring& path) {
    using namespace llcv::settings;
    AppSettings original;
    original.volumePercent = 35;
    original.exclusiveEndpointCache = {{L"keep-verified", true, 10, false}};
    unsigned long error = 99;
    if (!CheckSaved(path, original, "checked save succeeds")) return;
    if (!CheckWin32(WritePrivateProfileStringW(L"Future", L"PreservedKey", L"keep", path.c_str()),
                    "prepare unknown setting")) return;
    auto changed = original;
    changed.volumePercent = 90;
    changed.exclusiveEndpointCache.clear();
    if (!CheckWin32(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY),
                    "make profile read-only")) return;
    Check(!SaveToIni(path, changed, &error) && error == ERROR_ACCESS_DENIED,
          "read-only save reports failure");
    Check(LoadFromIni(path).settings.volumePercent == 35 &&
          LoadFromIni(path).settings.exclusiveEndpointCache.size() == 1,
          "failed save preserves volume and verification cache");
    if (!CheckWin32(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL),
                    "restore writable temporary profile")) return;
    const HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!CheckWin32(held != INVALID_HANDLE_VALUE,
                    "hold original profile without delete sharing")) return;
    Check(!SaveToIni(path, changed, &error) && error != ERROR_SUCCESS,
          "locked destination reports atomic replacement failure");
    CloseHandle(held);
    Check(LoadFromIni(path).settings.volumePercent == 35,
          "replacement failure never partially updates original");
    WIN32_FIND_DATAW entry{};
    const HANDLE remaining = FindFirstFileW((path + L".tmp-*").c_str(), &entry);
    Check(remaining == INVALID_HANDLE_VALUE, "failed save cleans only its owned temporary file");
    if (remaining != INVALID_HANDLE_VALUE) FindClose(remaining);
    if (!CheckSaved(path, changed, "save recovers once file lock is released")) return;
    wchar_t unknown[32]{};
    GetPrivateProfileStringW(L"Future", L"PreservedKey", L"", unknown, 32, path.c_str());
    Check(std::wstring(unknown) == L"keep", "atomic save preserves unknown settings");
    Check(!SaveToIni(path + L"\\missing\\settings.ini", changed, &error) && error != 0,
          "missing directory is a visible save failure");
    Check(!SaveToIni(L"", changed, &error) && error == ERROR_INVALID_NAME,
          "empty profile path is rejected");
}

}  // namespace

int main(int argc, char** argv) {
    // CTest runs these in isolation with WILL_FAIL: exactly one recorded
    // assertion must produce exit 1. A crash still fails CTest, and any missing
    // or cascading assertion returns 0 so WILL_FAIL rejects that regression.
    if (argc == 2 && std::strcmp(argv[1], "--save-failure-probe") == 0) {
        TestRoundTrip(L""); // Deterministic ERROR_INVALID_NAME, no filesystem writes.
        return failures == 1 ? 1 : 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--empty-cache-probe") == 0) {
        CheckExclusiveCacheRoundTrip(llcv::settings::AppSettings{});
        return failures == 1 ? 1 : 0;
    }
    if (argc != 1) {
        std::fprintf(stderr, "Unknown SettingsStoreTests argument.\n");
        return 2;
    }
    const std::wstring path = TemporaryIniPath();
    if (path.empty()) {
        std::fprintf(stderr, "FAILED: could not create temporary INI path\n");
        return 1;
    }
    TestDefaults(path);
    TestNewUnicodeProfile(path);
    TestResolutionRoundTrips(path);
    TestRoundTrip(path);
    for (const wchar_t* version : {L"1", L"0"}) {
        WritePrivateProfileStringW(L"ExclusiveEndpointCache", L"ProbeVersion", version, path.c_str());
        auto migrated = llcv::settings::LoadFromIni(path).settings;
        Check(migrated.exclusiveEndpointCache.empty() &&
              migrated.exclusiveVerifiedEndpointId.empty() && migrated.exclusiveVerifiedBufferMs == 0,
              "old probe and legacy verification invalidated together");
        migrated.exclusiveEndpointCache = {{L"checked-once", false, 0, true}};
        if (!CheckSaved(path, migrated, "save migrated endpoint cache")) continue;
        const auto reopened = llcv::settings::LoadFromIni(path).settings;
        Check(reopened.exclusiveEndpointCache.size() == 1 &&
              reopened.exclusiveEndpointCache.front().inconclusive,
              "new version preserves completed attempt across launches");
    }
    {
        auto settings = llcv::settings::LoadFromIni(path).settings;
        settings.roundedCorners = true;
        if (CheckSaved(path, settings, "save enabled rounded corners"))
            Check(llcv::settings::LoadFromIni(path).settings.roundedCorners, "rounded corners ON persists");
    }
    for (const wchar_t* value : {L"Dark", L"invalid", L"LIGHT"}) {
        WritePrivateProfileStringW(L"General", L"SettingsTheme", value, path.c_str());
        Check(llcv::settings::LoadFromIni(path).settings.settingsLightTheme ==
            (_wcsicmp(value, L"Light") == 0), "theme values are bounded and case insensitive");
    }
    WritePrivateProfileStringW(L"Window", L"AudioOnlyWidth", L"-1", path.c_str());
    WritePrivateProfileStringW(L"Window", L"AudioOnlyHeight", L"999999", path.c_str());
    const auto bounded = llcv::settings::LoadFromIni(path).settings;
    Check(bounded.audioOnlyWidth == 380 && bounded.audioOnlyHeight == 16384,
          "invalid audio-only dimensions are bounded");
    TestHdrChroma(path);
    TestPresentationModes(path);
    TestPcmDefaultAndPreservation(path);
    TestLegacyPcmMigration(path);
    TestSaveFailures(path);
    TestDaclPreservation(path);
    DeleteFileW(path.c_str());
    if (failures != 0) {
        std::fprintf(stderr, "%d test(s) failed.\n", failures);
        return 1;
    }
    std::puts("Settings store tests passed.");
    return 0;
}
