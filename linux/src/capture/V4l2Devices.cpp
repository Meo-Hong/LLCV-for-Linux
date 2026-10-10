#include "capture/V4l2Devices.h"

#include "capture/UvcDescriptors.h"
#include "diagnostics/Logger.h"
#include "platform/Strings.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <system_error>

#ifndef V4L2_PIX_FMT_P010
#define V4L2_PIX_FMT_P010 v4l2_fourcc('P', '0', '1', '0')
#endif

namespace llcv::capture {
namespace {

constexpr std::pair<int, int> kStepwiseSizes[] = {
    {3840, 2160}, {2560, 1440}, {1920, 1080}, {1280, 720}, {720, 480}, {640, 480},
};

int Ioctl(int fd, unsigned long request, void* argument) {
    int result = 0;
    do {
        result = ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

bool FormatFromFourcc(uint32_t fourcc, PixelFormat& format) {
    switch (fourcc) {
    case V4L2_PIX_FMT_NV12:
        format = PixelFormat::Nv12;
        return true;
    case V4L2_PIX_FMT_YUYV:
        format = PixelFormat::Yuyv;
        return true;
    case V4L2_PIX_FMT_MJPEG:
    case V4L2_PIX_FMT_JPEG:
        format = PixelFormat::Mjpeg;
        return true;
    case V4L2_PIX_FMT_BGR24:
        format = PixelFormat::Bgr24;
        return true;
    case V4L2_PIX_FMT_P010:
        format = PixelFormat::P010;
        return true;
    default:
        return false;
    }
}

int FormatPriority(PixelFormat format) {
    switch (format) {
    case PixelFormat::Nv12: return 0;
    case PixelFormat::Yuyv: return 1;
    case PixelFormat::Bgr24: return 2;
    case PixelFormat::Mjpeg: return 3;
    case PixelFormat::P010: return 4;
    }
    return 5;
}

std::string ReadLine(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return platform::Trim(line);
}

std::string UsbProductFor(const std::string& nodeName) {
    std::error_code error;
    auto directory = std::filesystem::canonical(
        std::filesystem::path("/sys/class/video4linux") / nodeName / "device", error);
    if (error) return {};
    while (!directory.empty() && directory != directory.root_path() && directory != "/sys") {
        if (std::filesystem::exists(directory / "product", error)) {
            return ReadLine(directory / "product");
        }
        directory = directory.parent_path();
    }
    return {};
}

std::map<std::string, std::string> StableNames() {
    std::map<std::string, std::string> names;
    for (const char* folder : {"/dev/v4l/by-path", "/dev/v4l/by-id"}) {
        std::error_code error;
        std::filesystem::directory_iterator iterator(folder, error);
        if (error) continue;
        for (const auto& entry : iterator) {
            const auto target = std::filesystem::canonical(entry.path(), error);
            if (error) continue;
            names[target.string()] = entry.path().string();
        }
    }
    return names;
}

int NodeNumber(const std::string& node) {
    const auto position = node.find_last_not_of("0123456789");
    if (position == std::string::npos || position + 1 >= node.size()) return 0;
    return std::atoi(node.c_str() + position + 1);
}

void SortRates(std::vector<FrameRate>& rates) {
    std::sort(rates.begin(), rates.end(),
              [](const FrameRate& a, const FrameRate& b) { return a.Fps() > b.Fps(); });
    rates.erase(std::unique(rates.begin(), rates.end(),
                            [](const FrameRate& a, const FrameRate& b) {
                                return a.Milli() == b.Milli();
                            }),
                rates.end());
}

std::vector<FrameRate> EnumerateRates(int fd, uint32_t fourcc, int width, int height) {
    std::vector<FrameRate> rates;
    v4l2_frmivalenum interval{};
    interval.pixel_format = fourcc;
    interval.width = static_cast<uint32_t>(width);
    interval.height = static_cast<uint32_t>(height);
    for (interval.index = 0; Ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0;
         ++interval.index) {
        if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            if (interval.discrete.numerator && interval.discrete.denominator) {
                rates.push_back({interval.discrete.numerator, interval.discrete.denominator});
            }
            continue;
        }
        const auto& minimum = interval.stepwise.min;
        const auto& maximum = interval.stepwise.max;
        if (minimum.numerator && minimum.denominator) {
            rates.push_back({minimum.numerator, minimum.denominator});
        }
        if (maximum.numerator && maximum.denominator) {
            rates.push_back({maximum.numerator, maximum.denominator});
        }
        break;
    }
    SortRates(rates);
    return rates;
}

std::vector<std::pair<int, int>> EnumerateSizes(int fd, uint32_t fourcc) {
    std::vector<std::pair<int, int>> sizes;
    v4l2_frmsizeenum size{};
    size.pixel_format = fourcc;
    for (size.index = 0; Ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0; ++size.index) {
        if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            sizes.emplace_back(static_cast<int>(size.discrete.width),
                               static_cast<int>(size.discrete.height));
            continue;
        }
        const auto& step = size.stepwise;
        for (const auto& [width, height] : kStepwiseSizes) {
            const auto w = static_cast<uint32_t>(width);
            const auto h = static_cast<uint32_t>(height);
            const bool inside = w >= step.min_width && w <= step.max_width &&
                                h >= step.min_height && h <= step.max_height;
            const bool aligned = (!step.step_width || (w - step.min_width) % step.step_width == 0) &&
                                 (!step.step_height || (h - step.min_height) % step.step_height == 0);
            if (inside && aligned) sizes.emplace_back(width, height);
        }
        break;
    }
    return sizes;
}

std::optional<CaptureDevice> Probe(const std::string& nodeName,
                                   const std::map<std::string, std::string>& stableNames) {
    const std::string node = "/dev/" + nodeName;
    const int fd = open(node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        diagnostics::Log("[capture] %s could not be opened: %s", node.c_str(), std::strerror(errno));
        return std::nullopt;
    }

    v4l2_capability capability{};
    if (Ioctl(fd, VIDIOC_QUERYCAP, &capability) < 0) {
        close(fd);
        return std::nullopt;
    }
    const uint32_t caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS)
                              ? capability.device_caps
                              : capability.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        close(fd);
        return std::nullopt;
    }

    CaptureDevice device;
    device.node = node;
    device.card = platform::Trim(reinterpret_cast<const char*>(capability.card));
    device.busInfo = platform::Trim(reinterpret_cast<const char*>(capability.bus_info));
    device.usbProduct = UsbProductFor(nodeName);
    device.advertisesP010 = UvcAdvertisesP010(nodeName);
    const auto stable = stableNames.find(node);
    device.id = stable != stableNames.end() ? stable->second : node;

    v4l2_fmtdesc description{};
    description.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (description.index = 0; Ioctl(fd, VIDIOC_ENUM_FMT, &description) == 0; ++description.index) {
        PixelFormat format{};
        if (!FormatFromFourcc(description.pixelformat, format)) continue;
        for (const auto& [width, height] : EnumerateSizes(fd, description.pixelformat)) {
            CaptureMode mode;
            mode.format = format;
            mode.width = width;
            mode.height = height;
            mode.rates = EnumerateRates(fd, description.pixelformat, width, height);
            if (!mode.rates.empty()) device.modes.push_back(std::move(mode));
        }
    }
    close(fd);
    if (device.modes.empty()) return std::nullopt;
    return device;
}

int DeviceScore(const CaptureDevice& device) {
    static constexpr const char* kCaptureKeywords[] = {
        "live gamer", "gc5", "avermedia", "elgato", "cam link", "capture", "hdmi", "usb video",
    };
    const std::string name = device.Name();
    for (const char* keyword : kCaptureKeywords) {
        if (platform::ContainsIgnoreCase(name, keyword)) return 2;
    }
    return device.usbProduct.empty() ? 0 : 1;
}

}

int FrameRate::Milli() const {
    return static_cast<int>(std::lround(Fps() * 1000.0));
}

bool CaptureDevice::Supports(PixelFormat format) const {
    for (const auto& mode : modes) {
        if (mode.format == format) return true;
    }
    return false;
}

std::string CaptureDevice::Name() const {
    return usbProduct.empty() ? card : usbProduct;
}

std::string CaptureDevice::DisplayName() const {
    return Name() + " (" + node + ")";
}

const char* PixelFormatName(PixelFormat format) {
    switch (format) {
    case PixelFormat::Nv12: return "NV12";
    case PixelFormat::Yuyv: return "YUY2";
    case PixelFormat::Mjpeg: return "MJPEG";
    case PixelFormat::Bgr24: return "BGR24";
    case PixelFormat::P010: return "P010";
    }
    return "?";
}

uint32_t FourccFor(PixelFormat format) {
    switch (format) {
    case PixelFormat::Nv12: return V4L2_PIX_FMT_NV12;
    case PixelFormat::Yuyv: return V4L2_PIX_FMT_YUYV;
    case PixelFormat::Mjpeg: return V4L2_PIX_FMT_MJPEG;
    case PixelFormat::Bgr24: return V4L2_PIX_FMT_BGR24;
    case PixelFormat::P010: return V4L2_PIX_FMT_P010;
    }
    return 0;
}

bool IsCompressed(PixelFormat format) {
    return format == PixelFormat::Mjpeg;
}

bool IsHdrOnly(PixelFormat format) {
    return format == PixelFormat::P010;
}

std::string FormatRate(const FrameRate& rate) {
    const double fps = rate.Fps();
    if (std::abs(fps - std::round(fps)) < 0.005) {
        return platform::Format("%d", static_cast<int>(std::lround(fps)));
    }
    return platform::Format("%.2f", fps);
}

std::vector<CaptureDevice> EnumerateCaptureDevices() {
    std::vector<CaptureDevice> devices;
    const auto stableNames = StableNames();
    std::error_code error;
    std::filesystem::directory_iterator iterator("/sys/class/video4linux", error);
    if (error) return devices;
    std::vector<std::string> nodes;
    for (const auto& entry : iterator) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("video", 0) == 0) nodes.push_back(name);
    }
    std::sort(nodes.begin(), nodes.end(), [](const std::string& a, const std::string& b) {
        return NodeNumber(a) < NodeNumber(b);
    });
    for (const auto& node : nodes) {
        if (auto device = Probe(node, stableNames)) devices.push_back(std::move(*device));
    }
    return devices;
}

const CaptureDevice* SelectDevice(const std::vector<CaptureDevice>& devices, const std::string& id) {
    if (!id.empty()) {
        for (const auto& device : devices) {
            if (device.id == id || device.node == id) return &device;
        }
        return nullptr;
    }
    const CaptureDevice* best = nullptr;
    int bestScore = -1;
    for (const auto& device : devices) {
        const int score = DeviceScore(device);
        if (score > bestScore) {
            best = &device;
            bestScore = score;
        }
    }
    return best;
}

std::vector<std::pair<int, int>> Resolutions(const CaptureDevice& device) {
    std::vector<std::pair<int, int>> sizes;
    for (const auto& mode : device.modes) sizes.emplace_back(mode.width, mode.height);
    std::sort(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) {
        const long areaA = static_cast<long>(a.first) * a.second;
        const long areaB = static_cast<long>(b.first) * b.second;
        return areaA != areaB ? areaA > areaB : a.first > b.first;
    });
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    return sizes;
}

std::vector<PixelFormat> FormatsAt(const CaptureDevice& device, int width, int height) {
    std::vector<PixelFormat> formats;
    for (const auto& mode : device.modes) {
        if (mode.width != width || mode.height != height) continue;
        if (std::find(formats.begin(), formats.end(), mode.format) == formats.end()) {
            formats.push_back(mode.format);
        }
    }
    std::sort(formats.begin(), formats.end(), [](PixelFormat a, PixelFormat b) {
        return FormatPriority(a) < FormatPriority(b);
    });
    return formats;
}

std::vector<FrameRate> RatesAt(const CaptureDevice& device, int width, int height,
                               std::optional<PixelFormat> format) {
    std::vector<FrameRate> rates;
    for (const auto& mode : device.modes) {
        if (mode.width != width || mode.height != height) continue;
        if (format ? mode.format != *format : IsHdrOnly(mode.format)) continue;
        rates.insert(rates.end(), mode.rates.begin(), mode.rates.end());
    }
    SortRates(rates);
    return rates;
}

std::optional<ResolvedMode> ResolveMode(const CaptureDevice& device, const ModeRequest& request) {
    std::vector<const CaptureMode*> candidates;
    for (const auto& mode : device.modes) {
        if (mode.width != request.width || mode.height != request.height) continue;
        if (request.format ? mode.format != *request.format : IsHdrOnly(mode.format)) continue;
        candidates.push_back(&mode);
    }
    if (candidates.empty()) return std::nullopt;
    std::stable_sort(candidates.begin(), candidates.end(), [](const CaptureMode* a, const CaptureMode* b) {
        return FormatPriority(a->format) < FormatPriority(b->format);
    });

    const auto make = [&](const CaptureMode* mode, const FrameRate& rate) {
        return ResolvedMode{mode->format, mode->width, mode->height, rate};
    };

    if (request.fpsMilli > 0) {
        for (const CaptureMode* mode : candidates) {
            for (const auto& rate : mode->rates) {
                if (std::abs(rate.Milli() - request.fpsMilli) <= 10) return make(mode, rate);
            }
        }
        const CaptureMode* bestMode = nullptr;
        FrameRate bestRate;
        int bestDistance = 0;
        for (const CaptureMode* mode : candidates) {
            for (const auto& rate : mode->rates) {
                const int distance = std::abs(rate.Milli() - request.fpsMilli);
                if (!bestMode || distance < bestDistance) {
                    bestMode = mode;
                    bestRate = rate;
                    bestDistance = distance;
                }
            }
        }
        return make(bestMode, bestRate);
    }

    std::vector<const CaptureMode*> pool;
    for (const CaptureMode* mode : candidates) {
        if (!IsCompressed(mode->format)) pool.push_back(mode);
    }
    if (pool.empty()) pool = candidates;
    const CaptureMode* best = pool.front();
    for (const CaptureMode* mode : pool) {
        if (mode->rates.front().Fps() > best->rates.front().Fps() + 0.01) best = mode;
    }
    return make(best, best->rates.front());
}

}
