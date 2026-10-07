#include "app/ModeSelection.h"

namespace llcv::app {

std::optional<capture::PixelFormat> FormatFromChoice(settings::PixelFormatChoice choice) {
    switch (choice) {
    case settings::PixelFormatChoice::Nv12: return capture::PixelFormat::Nv12;
    case settings::PixelFormatChoice::Yuyv: return capture::PixelFormat::Yuyv;
    case settings::PixelFormatChoice::Mjpeg: return capture::PixelFormat::Mjpeg;
    case settings::PixelFormatChoice::Bgr24: return capture::PixelFormat::Bgr24;
    case settings::PixelFormatChoice::P010: return capture::PixelFormat::P010;
    case settings::PixelFormatChoice::Auto: break;
    }
    return std::nullopt;
}

settings::PixelFormatChoice ChoiceFromFormat(capture::PixelFormat format) {
    switch (format) {
    case capture::PixelFormat::Nv12: return settings::PixelFormatChoice::Nv12;
    case capture::PixelFormat::Yuyv: return settings::PixelFormatChoice::Yuyv;
    case capture::PixelFormat::Mjpeg: return settings::PixelFormatChoice::Mjpeg;
    case capture::PixelFormat::Bgr24: return settings::PixelFormatChoice::Bgr24;
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
    return request;
}

}
