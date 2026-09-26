#pragma once

#include "settings/AppSettings.h"
#include <unknwn.h>
#include <string_view>

namespace llcv::capture {

enum class ToneMappingStatus {
    NotApplicable, NoInterface, Unsupported, QueryFailed, SetFailed, Applied,
    AmbiguousDevice
};

enum class ToneMappingVendor { None, Elgato, AverMedia, AverMediaUvc, ElgatoHid };

struct ToneMappingResult {
    ToneMappingStatus status = ToneMappingStatus::NotApplicable;
    ToneMappingVendor vendor = ToneMappingVendor::None;
    bool enable = false;
    HRESULT result = S_FALSE;
    DWORD support = 0;
};

// Startup only, on the selected video filter after SetFormat and before
// connecting/running the graph. Known driver properties are capability-probed.
// USB fallback requires the selected video path and an unambiguous matching
// physical device container. No commands are sent to other devices.
// P010 requests preserved input transfer/gamut (even without Force HDR10).
// SDR capture formats request hardware HDR->SDR conversion when supported.
// A successful Set is command acceptance, not proof of the incoming encoding.
ToneMappingResult ConfigureHardwareToneMapping(
    IUnknown* selectedFilter, std::wstring_view selectedName,
    settings::VideoPixelFormat negotiatedFormat,
    void (*log)(const wchar_t*) = nullptr,
    std::wstring_view selectedDevicePath = {});

} // namespace llcv::capture
