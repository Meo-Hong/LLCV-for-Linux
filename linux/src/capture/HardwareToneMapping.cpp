#include "capture/HardwareToneMapping.h"

#include "diagnostics/Logger.h"

#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>
#include <vector>

namespace llcv::capture {
namespace {

constexpr uint8_t kInterfaceDescriptor = 0x04;
constexpr uint8_t kClassSpecificInterface = 0x24;
constexpr uint8_t kVideoClass = 0x0E;
constexpr uint8_t kVideoControl = 0x01;
constexpr uint8_t kExtensionUnit = 0x06;
constexpr uint8_t kAverSelector = 11;
constexpr uint16_t kElgatoVendor = 0x0fd9;
constexpr uint16_t kElgatoHd60SPlus = 0x006a;

constexpr std::array<uint8_t, 16> kAverUvcGuid = {
    0x1b, 0x26, 0x35, 0xc8, 0x1c, 0xff, 0x9a, 0x4c, 0xb2, 0xf7, 0x93, 0xc9, 0x1f, 0xcf, 0xbe, 0x77,
};

struct UsbDevice {
    std::filesystem::path directory;
    uint16_t vendor = 0;
    uint16_t product = 0;
};

uint16_t ReadHex(const std::filesystem::path& file) {
    std::ifstream stream(file);
    std::string text;
    stream >> text;
    return static_cast<uint16_t>(std::strtoul(text.c_str(), nullptr, 16));
}

std::filesystem::path UsbParent(std::filesystem::path directory) {
    std::error_code error;
    while (!directory.empty() && directory != directory.root_path() && directory != "/sys") {
        if (std::filesystem::exists(directory / "idVendor", error) &&
            std::filesystem::exists(directory / "descriptors", error)) {
            return directory;
        }
        directory = directory.parent_path();
    }
    return {};
}

UsbDevice UsbDeviceFor(const std::string& devicePath) {
    std::error_code error;
    const auto node = std::filesystem::canonical(devicePath, error);
    if (error) return {};
    const auto device =
        std::filesystem::canonical(std::filesystem::path("/sys/class/video4linux") / node.filename() / "device", error);
    if (error) return {};
    UsbDevice usb;
    usb.directory = UsbParent(device);
    if (usb.directory.empty()) return {};
    usb.vendor = ReadHex(usb.directory / "idVendor");
    usb.product = ReadHex(usb.directory / "idProduct");
    return usb;
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> AverExtensionUnits(const UsbDevice& usb) {
    const std::vector<uint8_t> data = ReadFile(usb.directory / "descriptors");
    std::vector<uint8_t> units;
    bool control = false;
    size_t offset = 0;
    while (offset + 2 <= data.size()) {
        const size_t length = data[offset];
        if (length < 2 || offset + length > data.size()) break;
        const uint8_t type = data[offset + 1];
        if (type == kInterfaceDescriptor && length >= 9) {
            control = data[offset + 5] == kVideoClass && data[offset + 6] == kVideoControl;
        } else if (type == kClassSpecificInterface && control && length >= 21 && data[offset + 2] == kExtensionUnit &&
                   std::memcmp(data.data() + offset + 4, kAverUvcGuid.data(), kAverUvcGuid.size()) == 0) {
            units.push_back(data[offset + 3]);
        }
        offset += length;
    }
    return units;
}

int XuQuery(int fd, uint8_t unit, uint8_t query, uint16_t size, uint8_t* data) {
    uvc_xu_control_query request{};
    request.unit = unit;
    request.selector = kAverSelector;
    request.query = query;
    request.size = size;
    request.data = data;
    int result = 0;
    do {
        result = ioctl(fd, UVCIOC_CTRL_QUERY, &request);
    } while (result == -1 && errno == EINTR);
    return result == 0 ? 0 : errno;
}

ToneMappingResult AverUvcRequest(int fd, const UsbDevice& usb, bool enable) {
    ToneMappingResult result;
    result.vendor = ToneMappingVendor::AverMediaUvc;
    result.enable = enable;
    const std::vector<uint8_t> units = AverExtensionUnits(usb);
    if (units.empty()) {
        result.status = ToneMappingStatus::Unsupported;
        return result;
    }
    if (units.size() > 1) {
        result.status = ToneMappingStatus::AmbiguousDevice;
        return result;
    }
    const uint8_t unit = units.front();
    std::array<uint8_t, 2> lengthBytes{};
    result.error = XuQuery(fd, unit, UVC_GET_LEN, static_cast<uint16_t>(lengthBytes.size()), lengthBytes.data());
    const uint16_t length = static_cast<uint16_t>(lengthBytes[0] | (lengthBytes[1] << 8));
    if (result.error != 0 || length < 18 || length > 20) {
        result.status = result.error == ENOENT || result.error == EINVAL ? ToneMappingStatus::Unsupported
                                                                          : ToneMappingStatus::QueryFailed;
        return result;
    }
    std::array<uint8_t, 20> data{};
    result.error = XuQuery(fd, unit, UVC_GET_CUR, length, data.data());
    if (result.error != 0) {
        result.status = ToneMappingStatus::QueryFailed;
        return result;
    }
    data[15] = 0x02;
    data[17] = enable ? 1 : 0;
    result.error = XuQuery(fd, unit, UVC_SET_CUR, length, data.data());
    result.status = result.error == 0 ? ToneMappingStatus::Applied : ToneMappingStatus::SetFailed;
    return result;
}

bool IsKnownElgatoHid(const UsbDevice& usb) {
    return usb.vendor == kElgatoVendor && (usb.product == 0x006a || usb.product == 0x0082 || usb.product == 0x008a);
}

size_t OutputReportBytes(const uint8_t* descriptor, size_t length) {
    struct Globals {
        uint32_t size = 0;
        uint32_t count = 0;
        uint32_t id = 0;
    };
    Globals globals;
    std::vector<Globals> stack;
    std::map<uint32_t, uint32_t> bits;
    size_t offset = 0;
    while (offset < length) {
        const uint8_t prefix = descriptor[offset];
        if (prefix == 0xFE) {
            if (offset + 1 >= length) break;
            offset += 3 + descriptor[offset + 1];
            continue;
        }
        const size_t size = (prefix & 0x03) == 3 ? 4 : (prefix & 0x03);
        if (offset + 1 + size > length) break;
        uint32_t value = 0;
        for (size_t i = 0; i < size; ++i) value |= static_cast<uint32_t>(descriptor[offset + 1 + i]) << (8 * i);
        const uint8_t type = (prefix >> 2) & 0x03;
        const uint8_t tag = prefix >> 4;
        if (type == 1) {
            switch (tag) {
            case 7: globals.size = value; break;
            case 8: globals.id = value; break;
            case 9: globals.count = value; break;
            case 10: stack.push_back(globals); break;
            case 11:
                if (!stack.empty()) {
                    globals = stack.back();
                    stack.pop_back();
                }
                break;
            default: break;
            }
        } else if (type == 0 && tag == 9) {
            bits[globals.id] += globals.size * globals.count;
        }
        offset += 1 + size;
    }
    uint32_t largest = 0;
    for (const auto& entry : bits) largest = std::max(largest, entry.second);
    if (largest == 0) return 0;
    return (largest + 7) / 8 + 1;
}

size_t OutputReportBytes(int fd) {
    int descriptorSize = 0;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &descriptorSize) < 0 || descriptorSize <= 0 ||
        descriptorSize > HID_MAX_DESCRIPTOR_SIZE) {
        return 0;
    }
    hidraw_report_descriptor descriptor{};
    descriptor.size = static_cast<uint32_t>(descriptorSize);
    if (ioctl(fd, HIDIOCGRDESC, &descriptor) < 0) return 0;
    return OutputReportBytes(descriptor.value, descriptor.size);
}

std::vector<uint8_t> ElgatoReport(uint16_t product, size_t bytes, bool enable) {
    const bool old = product == kElgatoHd60SPlus;
    if (bytes < (old ? 5u : 7u) || bytes > 4096) return {};
    const uint8_t state = enable ? 1 : 0;
    std::vector<uint8_t> report;
    report.reserve(bytes);
    if (old) {
        report = {11, 0x55, 0x0a, 1, state};
    } else {
        report = {6, 6, 6, 0x55, 2, 0x0a, state};
    }
    report.resize(bytes, 0);
    return report;
}

ToneMappingResult ElgatoHidRequest(const UsbDevice& usb, bool enable) {
    ToneMappingResult result;
    result.vendor = ToneMappingVendor::ElgatoHid;
    result.enable = enable;
    std::error_code error;
    std::filesystem::directory_iterator iterator("/sys/class/hidraw", error);
    if (error) {
        result.status = ToneMappingStatus::Unsupported;
        return result;
    }
    std::string chosen;
    std::vector<uint8_t> report;
    int lastError = 0;
    for (const auto& entry : iterator) {
        const auto device = std::filesystem::canonical(entry.path() / "device", error);
        if (error || UsbParent(device) != usb.directory) continue;
        const std::string node = "/dev/" + entry.path().filename().string();
        const int fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            lastError = errno;
            continue;
        }
        hidraw_devinfo info{};
        const bool same = ioctl(fd, HIDIOCGRAWINFO, &info) == 0 && static_cast<uint16_t>(info.vendor) == usb.vendor &&
                          static_cast<uint16_t>(info.product) == usb.product;
        const size_t bytes = same ? OutputReportBytes(fd) : 0;
        close(fd);
        std::vector<uint8_t> candidate = ElgatoReport(usb.product, bytes, enable);
        if (candidate.empty()) continue;
        if (!chosen.empty()) {
            result.status = ToneMappingStatus::AmbiguousDevice;
            return result;
        }
        chosen = node;
        report = std::move(candidate);
    }
    if (chosen.empty()) {
        result.error = lastError;
        result.status = lastError != 0 ? ToneMappingStatus::QueryFailed : ToneMappingStatus::Unsupported;
        return result;
    }
    const int fd = open(chosen.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        result.error = errno;
        result.status = ToneMappingStatus::SetFailed;
        return result;
    }
#ifdef HIDIOCSOUTPUT
    const bool written = ioctl(fd, HIDIOCSOUTPUT(report.size()), report.data()) >= 0;
#else
    const bool written = write(fd, report.data(), report.size()) == static_cast<ssize_t>(report.size());
#endif
    result.error = written ? 0 : errno;
    close(fd);
    result.status = written ? ToneMappingStatus::Applied : ToneMappingStatus::SetFailed;
    return result;
}

void Report(const ToneMappingResult& result) {
    const char* vendor = result.vendor == ToneMappingVendor::AverMediaUvc ? "AVerMedia UVC extension"
                                                                           : "Elgato USB/HID";
    const char* status = "not applicable";
    switch (result.status) {
    case ToneMappingStatus::Applied: status = "command accepted (input encoding not verified)"; break;
    case ToneMappingStatus::Unsupported: status = "skipped: control not present"; break;
    case ToneMappingStatus::QueryFailed: status = "skipped: control query failed"; break;
    case ToneMappingStatus::SetFailed: status = "failed: device rejected the command"; break;
    case ToneMappingStatus::AmbiguousDevice: status = "skipped: ambiguous control"; break;
    case ToneMappingStatus::NotApplicable: break;
    }
    diagnostics::Log("[capture-hdr] %s hardware HDR-to-SDR: requested %s; %s%s%s", vendor, result.enable ? "on" : "off",
                     status, result.error ? " · " : "", result.error ? std::strerror(result.error) : "");
}

}

ToneMappingResult ConfigureHardwareToneMapping(int videoFd, const std::string& devicePath, PixelFormat format,
                                               bool preserveHdr) {
    const bool enable = format != PixelFormat::P010 && !preserveHdr;
    const UsbDevice usb = UsbDeviceFor(devicePath);
    if (usb.directory.empty()) return {};
    ToneMappingResult result = AverUvcRequest(videoFd, usb, enable);
    if (result.status != ToneMappingStatus::Unsupported) {
        Report(result);
        return result;
    }
    if (IsKnownElgatoHid(usb)) {
        result = ElgatoHidRequest(usb, enable);
        Report(result);
        return result;
    }
    return {};
}

}
