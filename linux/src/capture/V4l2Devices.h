#pragma once

#include "capture/VideoFrame.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llcv::capture {

struct FrameRate {
    uint32_t numerator = 1;
    uint32_t denominator = 60;

    double Fps() const {
        return numerator ? static_cast<double>(denominator) / static_cast<double>(numerator) : 0.0;
    }
    int Milli() const;
};

struct CaptureMode {
    PixelFormat format = PixelFormat::Nv12;
    int width = 0;
    int height = 0;
    std::vector<FrameRate> rates;
};

struct CaptureDevice {
    std::string id;
    std::string node;
    std::string card;
    std::string busInfo;
    std::string usbProduct;
    bool advertisesP010 = false;
    std::vector<CaptureMode> modes;

    bool Supports(PixelFormat format) const;
    std::string Name() const;
    std::string DisplayName() const;
};

struct ModeRequest {
    int width = 1920;
    int height = 1080;
    std::optional<PixelFormat> format;
    int fpsMilli = 0;
};

struct ResolvedMode {
    PixelFormat format = PixelFormat::Nv12;
    int width = 0;
    int height = 0;
    FrameRate rate;
};

const char* PixelFormatName(PixelFormat format);
uint32_t FourccFor(PixelFormat format);
bool IsCompressed(PixelFormat format);
bool IsHdrOnly(PixelFormat format);
std::string FormatRate(const FrameRate& rate);

std::vector<CaptureDevice> EnumerateCaptureDevices();
const CaptureDevice* SelectDevice(const std::vector<CaptureDevice>& devices, const std::string& id);
std::vector<std::pair<int, int>> Resolutions(const CaptureDevice& device);
std::vector<PixelFormat> FormatsAt(const CaptureDevice& device, int width, int height);
std::vector<FrameRate> RatesAt(const CaptureDevice& device, int width, int height,
                               std::optional<PixelFormat> format);
std::optional<ResolvedMode> ResolveMode(const CaptureDevice& device, const ModeRequest& request);

}
