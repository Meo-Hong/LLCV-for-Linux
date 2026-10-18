#pragma once

#include "capture/VideoFrame.h"

#include <string>

namespace llcv::capture {

enum class ToneMappingVendor { None, AverMediaUvc, ElgatoHid };
enum class ToneMappingStatus { NotApplicable, Unsupported, QueryFailed, SetFailed, Applied, AmbiguousDevice };

struct ToneMappingResult {
    ToneMappingStatus status = ToneMappingStatus::NotApplicable;
    ToneMappingVendor vendor = ToneMappingVendor::None;
    bool enable = false;
    int error = 0;
};

ToneMappingResult ConfigureHardwareToneMapping(int videoFd, const std::string& devicePath, PixelFormat format,
                                               bool preserveHdr);

}
