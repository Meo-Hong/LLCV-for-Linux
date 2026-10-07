#pragma once

#include "video/VideoColor.h"

#include <string>

namespace llcv::settings {

enum class UiLanguage { Auto, Korean, English };
enum class DisplayBackend { Auto, Wayland, X11 };
enum class PixelFormatChoice { Auto, Nv12, Yuyv, Mjpeg, Bgr24, P010 };
enum class PresentationMode { Immediate, VSync };
enum class ScalingMode { Smooth, Sharp };
enum class DriftCorrection { Off, Auto };
enum class VolumeHudPosition { TopLeft, TopRight, BottomLeft, BottomRight };
enum class FullscreenCursor { AutoHide, AlwaysVisible };
enum class HdrOutputMode { Auto, ToneMap, Hdr };

inline constexpr const char* kCaptureAudioDisabled = "none";
inline constexpr int kDeviceFrameOptions[] = {128, 256, 512, 1024};
inline constexpr int kPcmTargetOptions[] = {10, 15, 20, 25, 30, 40};
inline constexpr int kSdrWhiteOptions[] = {100, 160, 203, 250, 300};

struct AppSettings {
    UiLanguage uiLanguage = UiLanguage::Auto;
    bool lightTheme = false;
    DisplayBackend displayBackend = DisplayBackend::Auto;
    bool skipStartupSettings = false;
    bool saveLog = false;

    std::string captureDevice;
    int videoWidth = 1920;
    int videoHeight = 1080;
    PixelFormatChoice pixelFormat = PixelFormatChoice::Auto;
    int frameRateMilli = 0;
    PresentationMode presentationMode = PresentationMode::Immediate;
    ScalingMode scalingMode = ScalingMode::Smooth;
    video::ColorOverride colorOverride = video::ColorOverride::Auto;
    video::HdrInput hdrInput = video::HdrInput::Auto;
    HdrOutputMode hdrOutput = HdrOutputMode::Auto;
    int sdrWhiteNits = 203;
    bool screenshotClipboard = false;

    std::string captureAudioDevice;
    std::string audioOutputDevice;
    int audioDeviceFrames = 256;
    int pcmQueueTargetMs = 25;
    DriftCorrection driftCorrection = DriftCorrection::Auto;
    int volumePercent = 100;
    int leftVolumePercent = 100;
    int rightVolumePercent = 100;
    bool allowVolumeBoost = false;
    bool muteWhenBackground = false;
    VolumeHudPosition volumeHudPosition = VolumeHudPosition::TopLeft;
    bool audioOnly = false;
    int audioOnlyWidth = 420;
    int audioOnlyHeight = 280;

    bool pixelPerfect = true;
    bool borderlessWindow = false;
    bool startFullscreen = false;
    FullscreenCursor fullscreenCursor = FullscreenCursor::AutoHide;
    std::string preferredDisplay;
    int windowWidth = 0;
    int windowHeight = 0;
    bool hasWindowPosition = false;
    int windowX = 0;
    int windowY = 0;
};

}
