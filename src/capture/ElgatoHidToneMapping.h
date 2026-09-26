#pragma once
#include "HardwareToneMapping.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace llcv::capture {
struct HidToneMappingDevice {
    GUID container{};
    unsigned short vendor = 0, product = 0, reportBytes = 0;
    std::wstring path;
};

// Narrow startup-only transport seam: fake tests never enumerate/open hardware.
class ElgatoHidAccess {
public:
    virtual ~ElgatoHidAccess() = default;
    virtual HRESULT DescribeSelected(std::wstring_view videoPath, HidToneMappingDevice& device) = 0;
    virtual HRESULT FindCandidates(const HidToneMappingDevice& selected,
                                  std::vector<HidToneMappingDevice>& devices) = 0;
    virtual HRESULT Write(const HidToneMappingDevice& device, std::span<const uint8_t> report) = 0;
};
bool IsKnownElgatoHidProduct(unsigned short vendor, unsigned short product);
bool SameCaptureContainer(const HidToneMappingDevice& selected, const HidToneMappingDevice& candidate);
std::vector<uint8_t> ElgatoToneMappingReport(unsigned short product, unsigned short reportBytes, bool enable);
ToneMappingResult ConfigureElgatoHidToneMapping(std::wstring_view videoPath, bool enable, ElgatoHidAccess& access);
ToneMappingResult ConfigureElgatoHidToneMapping(std::wstring_view videoPath, bool enable);
}
