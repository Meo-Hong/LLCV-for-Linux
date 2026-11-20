#include "app/ModeSelection.h"

#include <cmath>

namespace llcv::app {

std::optional<capture::PixelFormat> FormatFromChoice(settings::PixelFormatChoice choice) {
    switch (choice) {
    case settings::PixelFormatChoice::Nv12: return capture::PixelFormat::Nv12;
    case settings::PixelFormatChoice::Yuyv: return capture::PixelFormat::Yuyv;
    case settings::PixelFormatChoice::Uyvy: return capture::PixelFormat::Uyvy;
    case settings::PixelFormatChoice::Mjpeg: return capture::PixelFormat::Mjpeg;
    case settings::PixelFormatChoice::Bgr24: return capture::PixelFormat::Bgr24;
    case settings::PixelFormatChoice::Bgr32: return capture::PixelFormat::Bgr32;
    case settings::PixelFormatChoice::Rgb24: return capture::PixelFormat::Rgb24;
    case settings::PixelFormatChoice::P010: return capture::PixelFormat::P010;
    case settings::PixelFormatChoice::Auto: break;
    }
    return std::nullopt;
}

settings::PixelFormatChoice ChoiceFromFormat(capture::PixelFormat format) {
    switch (format) {
    case capture::PixelFormat::Nv12: return settings::PixelFormatChoice::Nv12;
    case capture::PixelFormat::Yuyv: return settings::PixelFormatChoice::Yuyv;
    case capture::PixelFormat::Uyvy: return settings::PixelFormatChoice::Uyvy;
    case capture::PixelFormat::Mjpeg: return settings::PixelFormatChoice::Mjpeg;
    case capture::PixelFormat::Bgr24: return settings::PixelFormatChoice::Bgr24;
    case capture::PixelFormat::Bgr32: return settings::PixelFormatChoice::Bgr32;
    case capture::PixelFormat::Rgb24: return settings::PixelFormatChoice::Rgb24;
    case capture::PixelFormat::P010: return settings::PixelFormatChoice::P010;
    }
    return settings::PixelFormatChoice::Auto;
}

capture::ModeRequest RequestFromSettings(const settings::AppSettings& settings) {
    capture::ModeRequest request;
    request.width = settings.videoWidth;
    request.height = settings.videoHeight;
    request.format = FormatFromChoice(settings.pixelFormat);
    request.fpsMilli = settings.frameRateMilli;
    request.capture40Fps = settings.capture40Fps;
    request.allowUnlistedFps = settings.allowUnlistedFps;
    return request;
}

bool SelectedFpsUnavailable(const capture::CaptureDevice& device, const settings::AppSettings& settings) {
    capture::ModeRequest request = RequestFromSettings(settings);
    if (request.capture40Fps || request.fpsMilli <= 0 || capture::ResolveMode(device, request)) return false;
    request.fpsMilli = 0;
    return capture::ResolveMode(device, request).has_value();
}

int RequestedCaptureFps(const settings::AppSettings& settings) {
    if (settings.audioOnly) return 0;
    if (settings.capture40Fps) return 40;
    return settings.frameRateMilli > 0 ? static_cast<int>(std::lround(settings.frameRateMilli / 1000.0)) : 0;
}

int InterpolationFrameRateMilli(const settings::AppSettings& settings) {
    const auto supported = [](int milli) { return milli >= 23500 && milli <= 60500; };
    if (settings.rifeFrameRateMilli == 0 || supported(settings.rifeFrameRateMilli)) return settings.rifeFrameRateMilli;
    if (settings.capture40Fps) return 40000;
    return supported(settings.frameRateMilli) ? settings.frameRateMilli : 0;
}

std::optional<capture::ResolvedMode> ResolveInterpolationMode(const capture::CaptureDevice& device,
                                                             const settings::AppSettings& settings, bool& capture40) {
    capture::ModeRequest request = RequestFromSettings(settings);
    request.width = 1920;
    request.height = 1080;
    request.capture40Fps = false;
    capture40 = false;
    const int milli = InterpolationFrameRateMilli(settings);
    if (milli == 40000 && settings.capture40Fps) {
        request.capture40Fps = true;
        capture40 = true;
        return capture::ResolveMode(device, request);
    }
    if (milli > 0) {
        request.fpsMilli = milli;
        return capture::ResolveMode(device, request);
    }
    int best = 0;
    for (const auto& rate : capture::RatesAt(device, request.width, request.height, request.format)) {
        const int value = rate.Milli();
        if (value >= 23500 && value <= 60500 && value > best) best = value;
    }
    if (best == 0) return std::nullopt;
    request.fpsMilli = best;
    request.allowUnlistedFps = false;
    return capture::ResolveMode(device, request);
}

}
