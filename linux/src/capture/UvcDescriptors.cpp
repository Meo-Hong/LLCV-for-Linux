#include "capture/UvcDescriptors.h"

#include <sys/utsname.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace llcv::capture {
namespace {

constexpr uint8_t kInterfaceDescriptor = 0x04;
constexpr uint8_t kClassSpecificInterface = 0x24;
constexpr uint8_t kVideoClass = 0x0E;
constexpr uint8_t kVideoStreaming = 0x02;
constexpr uint8_t kFormatUncompressed = 0x04;

constexpr std::array<uint8_t, 16> kP010Guid = {
    'P', '0', '1', '0', 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71,
};

std::vector<uint8_t> ReadDescriptors(const std::string& nodeName) {
    std::error_code error;
    auto directory = std::filesystem::canonical(
        std::filesystem::path("/sys/class/video4linux") / nodeName / "device", error);
    if (error) return {};
    while (!directory.empty() && directory != directory.root_path() && directory != "/sys") {
        const auto file = directory / "descriptors";
        if (std::filesystem::exists(directory / "idVendor", error) && std::filesystem::exists(file, error)) {
            std::ifstream stream(file, std::ios::binary);
            return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        }
        directory = directory.parent_path();
    }
    return {};
}

}

bool UvcAdvertisesP010(const std::string& nodeName) {
    const std::vector<uint8_t> data = ReadDescriptors(nodeName);
    bool streamingInterface = false;
    size_t offset = 0;
    while (offset + 2 <= data.size()) {
        const size_t length = data[offset];
        if (length < 2 || offset + length > data.size()) break;
        const uint8_t type = data[offset + 1];
        if (type == kInterfaceDescriptor && length >= 9) {
            streamingInterface = data[offset + 5] == kVideoClass && data[offset + 6] == kVideoStreaming;
        } else if (type == kClassSpecificInterface && streamingInterface && length >= 21 &&
                   data[offset + 2] == kFormatUncompressed &&
                   std::memcmp(data.data() + offset + 5, kP010Guid.data(), kP010Guid.size()) == 0) {
            return true;
        }
        offset += length;
    }
    return false;
}

std::string KernelRelease() {
    utsname name{};
    if (uname(&name) != 0) return {};
    return name.release;
}

}
