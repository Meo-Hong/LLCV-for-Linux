#include "settings/SettingsStore.h"

#include "diagnostics/Logger.h"
#include "platform/Paths.h"
#include "platform/Strings.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace llcv::settings {
namespace {

using Values = std::map<std::string, std::string>;

template <typename E>
struct EnumName {
    E value;
    const char* name;
};

constexpr EnumName<UiLanguage> kLanguages[] = {
    {UiLanguage::Auto, "auto"}, {UiLanguage::Korean, "ko"}, {UiLanguage::English, "en"},
    {UiLanguage::Japanese, "ja"}, {UiLanguage::Spanish, "es"}, {UiLanguage::Portuguese, "pt-BR"}};
constexpr EnumName<DisplayBackend> kBackends[] = {
    {DisplayBackend::Auto, "auto"}, {DisplayBackend::Wayland, "wayland"}, {DisplayBackend::X11, "x11"}};
constexpr EnumName<PixelFormatChoice> kFormats[] = {
    {PixelFormatChoice::Auto, "auto"}, {PixelFormatChoice::Nv12, "nv12"},
    {PixelFormatChoice::Yuyv, "yuy2"}, {PixelFormatChoice::Mjpeg, "mjpeg"},
    {PixelFormatChoice::Bgr24, "bgr24"}, {PixelFormatChoice::P010, "p010"}};
constexpr EnumName<PresentationMode> kPresentation[] = {
    {PresentationMode::Immediate, "immediate"}, {PresentationMode::VSync, "vsync"}};
constexpr EnumName<ScalingMode> kScaling[] = {
    {ScalingMode::Smooth, "smooth"}, {ScalingMode::Sharp, "sharp"}};
constexpr EnumName<video::ColorOverride> kColors[] = {
    {video::ColorOverride::Auto, "auto"},
    {video::ColorOverride::Bt709Limited, "bt709-limited"},
    {video::ColorOverride::Bt709Full, "bt709-full"},
    {video::ColorOverride::Bt601Limited, "bt601-limited"},
    {video::ColorOverride::Bt601Full, "bt601-full"}};
constexpr EnumName<video::HdrInput> kHdrInput[] = {
    {video::HdrInput::Auto, "auto"}, {video::HdrInput::ForceHdr10, "hdr10"}, {video::HdrInput::ForceSdr, "sdr"}};
constexpr EnumName<HdrOutputMode> kHdrOutput[] = {
    {HdrOutputMode::Auto, "auto"}, {HdrOutputMode::ToneMap, "tonemap"}, {HdrOutputMode::Hdr, "hdr"}};
constexpr EnumName<DriftCorrection> kDrift[] = {
    {DriftCorrection::Auto, "auto"}, {DriftCorrection::Off, "off"}};
constexpr EnumName<VolumeHudPosition> kHud[] = {
    {VolumeHudPosition::TopLeft, "top-left"}, {VolumeHudPosition::TopRight, "top-right"},
    {VolumeHudPosition::BottomLeft, "bottom-left"}, {VolumeHudPosition::BottomRight, "bottom-right"}};
constexpr EnumName<FullscreenCursor> kCursor[] = {
    {FullscreenCursor::AutoHide, "auto-hide"}, {FullscreenCursor::AlwaysVisible, "visible"}};
constexpr EnumName<HdrChromaLocation> kHdrChroma[] = {
    {HdrChromaLocation::Auto, "auto"}, {HdrChromaLocation::Left, "left"}, {HdrChromaLocation::TopLeft, "top-left"}};
constexpr EnumName<FullscreenVideoSize> kFullscreenSize[] = {
    {FullscreenVideoSize::Fit, "fit"}, {FullscreenVideoSize::Original, "original"}};
constexpr EnumName<FullscreenVideoPosition> kFullscreenPosition[] = {
    {FullscreenVideoPosition::Center, "center"}, {FullscreenVideoPosition::TopLeft, "top-left"},
    {FullscreenVideoPosition::Top, "top"}, {FullscreenVideoPosition::TopRight, "top-right"},
    {FullscreenVideoPosition::Left, "left"}, {FullscreenVideoPosition::Right, "right"},
    {FullscreenVideoPosition::BottomLeft, "bottom-left"}, {FullscreenVideoPosition::Bottom, "bottom"},
    {FullscreenVideoPosition::BottomRight, "bottom-right"}};
constexpr EnumName<recording::Codec> kRecordingCodecs[] = {
    {recording::Codec::H264, "h264"}, {recording::Codec::Hevc, "hevc"}, {recording::Codec::Av1, "av1"}};
constexpr EnumName<recording::EncoderChoice> kRecordingEncoders[] = {
    {recording::EncoderChoice::AutoHardware, "auto"}, {recording::EncoderChoice::Cpu, "cpu"}};
constexpr EnumName<recording::RateControl> kRecordingRates[] = {
    {recording::RateControl::Quality, "quality"}, {recording::RateControl::Bitrate, "bitrate"}};
constexpr EnumName<recording::Quality> kRecordingQualities[] = {
    {recording::Quality::Balanced, "balanced"}, {recording::Quality::Compact, "compact"},
    {recording::Quality::High, "high"}};
constexpr EnumName<recording::SplitMode> kRecordingSplits[] = {
    {recording::SplitMode::Off, "off"}, {recording::SplitMode::Duration, "duration"},
    {recording::SplitMode::Size, "size"}};

template <typename E, size_t N>
E ReadEnum(const Values& values, const char* key, const EnumName<E> (&names)[N], E fallback) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    for (const auto& entry : names) {
        if (it->second == entry.name) return entry.value;
    }
    return fallback;
}

template <typename E, size_t N>
const char* EnumText(E value, const EnumName<E> (&names)[N]) {
    for (const auto& entry : names) {
        if (entry.value == value) return entry.name;
    }
    return names[0].name;
}

int ReadInt(const Values& values, const char* key, int fallback, int minimum, int maximum) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    const char* text = it->second.c_str();
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text, &end, 10);
    if (end == text || errno == ERANGE || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        return fallback;
    }
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end) return fallback;
    return static_cast<int>(std::clamp<long>(value, minimum, maximum));
}

bool ReadBool(const Values& values, const char* key, bool fallback) {
    const auto it = values.find(key);
    if (it == values.end()) return fallback;
    return it->second == "1" || it->second == "true" || it->second == "yes";
}

std::string ReadString(const Values& values, const char* key) {
    const auto it = values.find(key);
    return it == values.end() ? std::string{} : it->second;
}

template <size_t N>
int ReadOption(const Values& values, const char* key, int fallback, const int (&options)[N]) {
    const int value = ReadInt(values, key, fallback, 0, 1000000);
    return std::find(std::begin(options), std::end(options), value) != std::end(options) ? value : fallback;
}

Values Parse(const std::filesystem::path& path) {
    Values values;
    std::ifstream file(path);
    std::string line;
    std::string section;
    while (std::getline(file, line)) {
        const std::string trimmed = platform::Trim(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') continue;
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = platform::Trim(trimmed.substr(1, trimmed.size() - 2));
            continue;
        }
        const auto equals = trimmed.find('=');
        if (equals == std::string::npos) continue;
        values[section + "." + platform::Trim(trimmed.substr(0, equals))] =
            platform::Trim(trimmed.substr(equals + 1));
    }
    return values;
}

const char* Bool(bool value) {
    return value ? "1" : "0";
}

input::Bindings ReadShortcuts(const Values& values) {
    input::Bindings bindings = input::kDefaultBindings;
    std::vector<input::Action> missing;
    for (size_t i = 0; i < input::kActionCount; ++i) {
        const auto it = values.find(std::string("Shortcuts.") + input::kActionNames[i]);
        if (it == values.end()) {
            missing.push_back(static_cast<input::Action>(i));
            continue;
        }
        const auto binding = input::Parse(it->second);
        if (!binding) return input::kDefaultBindings;
        bindings[i] = *binding;
    }
    if (missing.size() < input::kActionCount) {
        for (const input::Action action : missing) input::AssignFreeDefault(bindings, action);
    }
    return input::IsValid(bindings) ? bindings : input::kDefaultBindings;
}

}

AppSettings LoadSettings(const std::filesystem::path& path) {
    AppSettings s;
    const Values v = Parse(path);
    if (v.empty()) return s;

    s.uiLanguage = ReadEnum(v, "App.Language", kLanguages, s.uiLanguage);
    s.lightTheme = ReadBool(v, "App.LightTheme", s.lightTheme);
    s.displayBackend = ReadEnum(v, "App.DisplayBackend", kBackends, s.displayBackend);
    s.skipStartupSettings = ReadBool(v, "App.SkipStartupSettings", s.skipStartupSettings);
    s.saveLog = ReadBool(v, "App.SaveLog", s.saveLog);
    s.preventSleep = ReadBool(v, "App.PreventSleep", s.preventSleep);

    s.captureDevice = ReadString(v, "Video.Device");
    s.videoWidth = ReadInt(v, "Video.Width", s.videoWidth, 160, 8192);
    s.videoHeight = ReadInt(v, "Video.Height", s.videoHeight, 120, 8192);
    s.pixelFormat = ReadEnum(v, "Video.PixelFormat", kFormats, s.pixelFormat);
    s.frameRateMilli = ReadInt(v, "Video.FrameRateMilli", s.frameRateMilli, 0, 1000000);
    s.capture40Fps = ReadBool(v, "Video.Capture40Fps", s.capture40Fps);
    s.allowUnlistedFps = ReadBool(v, "Video.AllowUnlistedFps", s.allowUnlistedFps);
    s.presentationMode = ReadEnum(v, "Video.Presentation", kPresentation, s.presentationMode);
    s.scalingMode = ReadEnum(v, "Video.Scaling", kScaling, s.scalingMode);
    s.colorOverride = ReadEnum(v, "Video.Color", kColors, s.colorOverride);
    s.hdrInput = ReadEnum(v, "Video.HdrInput", kHdrInput, s.hdrInput);
    s.hdrOutput = ReadEnum(v, "Video.HdrOutput", kHdrOutput, s.hdrOutput);
    s.hdrChromaLocation = ReadEnum(v, "Video.HdrChroma", kHdrChroma, s.hdrChromaLocation);
    s.sdrWhiteNits = ReadOption(v, "Video.SdrWhiteNits", s.sdrWhiteNits, kSdrWhiteOptions);
    s.screenshotClipboard = ReadBool(v, "Video.ScreenshotClipboard", s.screenshotClipboard);

    s.captureAudioDevice = ReadString(v, "Audio.CaptureDevice");
    s.audioOutputDevice = ReadString(v, "Audio.OutputDevice");
    s.audioDeviceFrames = ReadOption(v, "Audio.DeviceFrames", s.audioDeviceFrames, kDeviceFrameOptions);
    s.pcmQueueTargetMs = ReadOption(v, "Audio.PcmTargetMs", s.pcmQueueTargetMs, kPcmTargetOptions);
    s.driftCorrection = ReadEnum(v, "Audio.DriftCorrection", kDrift, s.driftCorrection);
    s.allowVolumeBoost = ReadBool(v, "Audio.AllowVolumeBoost", s.allowVolumeBoost);
    s.volumePercent = ReadInt(v, "Audio.Volume", s.volumePercent, 0, s.allowVolumeBoost ? 200 : 100);
    s.volumePercent -= s.volumePercent % 5;
    s.leftVolumePercent = ReadInt(v, "Audio.LeftVolume", s.leftVolumePercent, 0, 100);
    s.rightVolumePercent = ReadInt(v, "Audio.RightVolume", s.rightVolumePercent, 0, 100);
    s.muteWhenBackground = ReadBool(v, "Audio.MuteWhenBackground", s.muteWhenBackground);
    s.volumeHudPosition = ReadEnum(v, "Audio.VolumeHud", kHud, s.volumeHudPosition);
    s.audioOnly = ReadBool(v, "Audio.AudioOnly", s.audioOnly);
    s.audioOnlyWidth = ReadInt(v, "Audio.AudioOnlyWidth", s.audioOnlyWidth, 320, 4000);
    s.audioOnlyHeight = ReadInt(v, "Audio.AudioOnlyHeight", s.audioOnlyHeight, 220, 4000);

    s.pixelPerfect = ReadBool(v, "Window.PixelPerfect", s.pixelPerfect);
    s.borderlessWindow = ReadBool(v, "Window.Borderless", s.borderlessWindow);
    s.keepRelativeWindowSize = ReadBool(v, "Window.KeepRelativeSize", s.keepRelativeWindowSize);
    s.startFullscreen = ReadBool(v, "Window.StartFullscreen", s.startFullscreen);
    s.fullscreenCursor = ReadEnum(v, "Window.FullscreenCursor", kCursor, s.fullscreenCursor);
    s.fullscreenVideoSize = ReadEnum(v, "Window.FullscreenVideoSize", kFullscreenSize, s.fullscreenVideoSize);
    s.fullscreenVideoPosition =
        ReadEnum(v, "Window.FullscreenVideoPosition", kFullscreenPosition, s.fullscreenVideoPosition);
    s.preferredDisplay = ReadString(v, "Window.Display");
    s.windowWidth = ReadInt(v, "Window.Width", 0, 0, 16384);
    s.windowHeight = ReadInt(v, "Window.Height", 0, 0, 16384);
    s.hasWindowPosition = ReadBool(v, "Window.HasPosition", false);
    s.windowX = ReadInt(v, "Window.X", 0, -32768, 32767);
    s.windowY = ReadInt(v, "Window.Y", 0, -32768, 32767);

    recording::Options& r = s.recording;
    r.codec = ReadEnum(v, "Recording.Codec", kRecordingCodecs, r.codec);
    r.encoder = ReadEnum(v, "Recording.Encoder", kRecordingEncoders, r.encoder);
    r.rateControl = ReadEnum(v, "Recording.RateControl", kRecordingRates, r.rateControl);
    r.quality = ReadEnum(v, "Recording.Quality", kRecordingQualities, r.quality);
    r.videoMbps = ReadOption(v, "Recording.VideoMbps", r.videoMbps, recording::kVideoMbpsOptions);
    r.maxHeight = ReadOption(v, "Recording.MaxHeight", r.maxHeight, recording::kMaxHeightOptions);
    r.maxFps = ReadOption(v, "Recording.MaxFps", r.maxFps, recording::kMaxFpsOptions);
    r.audioKbps = ReadOption(v, "Recording.AudioKbps", r.audioKbps, recording::kAudioKbpsOptions);
    r.volumePercent = ReadInt(v, "Recording.Volume", r.volumePercent, 0, 100);
    r.splitMode = ReadEnum(v, "Recording.Split", kRecordingSplits, r.splitMode);
    r.splitMinutes = ReadOption(v, "Recording.SplitMinutes", r.splitMinutes, recording::kSplitMinutesOptions);
    r.splitGib = ReadOption(v, "Recording.SplitGiB", r.splitGib, recording::kSplitGibOptions);
    r.folder = ReadString(v, "Recording.Folder");

    s.shortcuts = ReadShortcuts(v);
    return s;
}

bool SaveSettings(const std::filesystem::path& path, const AppSettings& s) {
    if (!platform::EnsureDirectory(path.parent_path())) return false;
    const auto temporary = path.string() + ".tmp";
    bool written = false;
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) {
            diagnostics::Log("[settings] could not create %s", temporary.c_str());
            return false;
        }
        file << "[App]\n"
             << "Language=" << EnumText(s.uiLanguage, kLanguages) << "\n"
             << "LightTheme=" << Bool(s.lightTheme) << "\n"
             << "DisplayBackend=" << EnumText(s.displayBackend, kBackends) << "\n"
             << "SkipStartupSettings=" << Bool(s.skipStartupSettings) << "\n"
             << "SaveLog=" << Bool(s.saveLog) << "\n"
             << "PreventSleep=" << Bool(s.preventSleep) << "\n\n"
             << "[Video]\n"
             << "Device=" << s.captureDevice << "\n"
             << "Width=" << s.videoWidth << "\n"
             << "Height=" << s.videoHeight << "\n"
             << "PixelFormat=" << EnumText(s.pixelFormat, kFormats) << "\n"
             << "FrameRateMilli=" << s.frameRateMilli << "\n"
             << "Capture40Fps=" << Bool(s.capture40Fps) << "\n"
             << "AllowUnlistedFps=" << Bool(s.allowUnlistedFps) << "\n"
             << "Presentation=" << EnumText(s.presentationMode, kPresentation) << "\n"
             << "Scaling=" << EnumText(s.scalingMode, kScaling) << "\n"
             << "Color=" << EnumText(s.colorOverride, kColors) << "\n"
             << "HdrInput=" << EnumText(s.hdrInput, kHdrInput) << "\n"
             << "HdrOutput=" << EnumText(s.hdrOutput, kHdrOutput) << "\n"
             << "HdrChroma=" << EnumText(s.hdrChromaLocation, kHdrChroma) << "\n"
             << "SdrWhiteNits=" << s.sdrWhiteNits << "\n"
             << "ScreenshotClipboard=" << Bool(s.screenshotClipboard) << "\n\n"
             << "[Audio]\n"
             << "CaptureDevice=" << s.captureAudioDevice << "\n"
             << "OutputDevice=" << s.audioOutputDevice << "\n"
             << "DeviceFrames=" << s.audioDeviceFrames << "\n"
             << "PcmTargetMs=" << s.pcmQueueTargetMs << "\n"
             << "DriftCorrection=" << EnumText(s.driftCorrection, kDrift) << "\n"
             << "Volume=" << s.volumePercent << "\n"
             << "LeftVolume=" << s.leftVolumePercent << "\n"
             << "RightVolume=" << s.rightVolumePercent << "\n"
             << "AllowVolumeBoost=" << Bool(s.allowVolumeBoost) << "\n"
             << "MuteWhenBackground=" << Bool(s.muteWhenBackground) << "\n"
             << "VolumeHud=" << EnumText(s.volumeHudPosition, kHud) << "\n"
             << "AudioOnly=" << Bool(s.audioOnly) << "\n"
             << "AudioOnlyWidth=" << s.audioOnlyWidth << "\n"
             << "AudioOnlyHeight=" << s.audioOnlyHeight << "\n\n"
             << "[Window]\n"
             << "PixelPerfect=" << Bool(s.pixelPerfect) << "\n"
             << "Borderless=" << Bool(s.borderlessWindow) << "\n"
             << "KeepRelativeSize=" << Bool(s.keepRelativeWindowSize) << "\n"
             << "StartFullscreen=" << Bool(s.startFullscreen) << "\n"
             << "FullscreenCursor=" << EnumText(s.fullscreenCursor, kCursor) << "\n"
             << "FullscreenVideoSize=" << EnumText(s.fullscreenVideoSize, kFullscreenSize) << "\n"
             << "FullscreenVideoPosition=" << EnumText(s.fullscreenVideoPosition, kFullscreenPosition) << "\n"
             << "Display=" << s.preferredDisplay << "\n"
             << "Width=" << s.windowWidth << "\n"
             << "Height=" << s.windowHeight << "\n"
             << "HasPosition=" << Bool(s.hasWindowPosition) << "\n"
             << "X=" << s.windowX << "\n"
             << "Y=" << s.windowY << "\n\n"
             << "[Recording]\n"
             << "Codec=" << EnumText(s.recording.codec, kRecordingCodecs) << "\n"
             << "Encoder=" << EnumText(s.recording.encoder, kRecordingEncoders) << "\n"
             << "RateControl=" << EnumText(s.recording.rateControl, kRecordingRates) << "\n"
             << "Quality=" << EnumText(s.recording.quality, kRecordingQualities) << "\n"
             << "VideoMbps=" << s.recording.videoMbps << "\n"
             << "MaxHeight=" << s.recording.maxHeight << "\n"
             << "MaxFps=" << s.recording.maxFps << "\n"
             << "AudioKbps=" << s.recording.audioKbps << "\n"
             << "Volume=" << s.recording.volumePercent << "\n"
             << "Split=" << EnumText(s.recording.splitMode, kRecordingSplits) << "\n"
             << "SplitMinutes=" << s.recording.splitMinutes << "\n"
             << "SplitGiB=" << s.recording.splitGib << "\n"
             << "Folder=" << s.recording.folder << "\n\n"
             << "[Shortcuts]\n";
        const input::Bindings& shortcuts = input::IsValid(s.shortcuts) ? s.shortcuts : input::kDefaultBindings;
        for (size_t i = 0; i < input::kActionCount; ++i) {
            file << input::kActionNames[i] << "=" << input::Label(shortcuts[i]) << "\n";
        }
        file.flush();
        written = static_cast<bool>(file);
    }
    std::error_code error;
    if (!written) {
        diagnostics::Log("[settings] could not write %s; keeping the previous settings", temporary.c_str());
        std::filesystem::remove(temporary, error);
        return false;
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        diagnostics::Log("[settings] save failed: %s; keeping the previous settings", error.message().c_str());
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

}
