#pragma once

#include "capture/VideoFrame.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace llcv::video {

class MjpegDecoder {
public:
    MjpegDecoder();
    ~MjpegDecoder();
    MjpegDecoder(const MjpegDecoder&) = delete;
    MjpegDecoder& operator=(const MjpegDecoder&) = delete;

    bool Decode(const uint8_t* data, size_t size, capture::VideoFrame& frame);
    const std::string& LastError() const { return lastError_; }

private:
    void* handle_ = nullptr;
    std::string lastError_;
};

}
