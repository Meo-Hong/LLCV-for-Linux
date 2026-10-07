#pragma once

#include "video/VideoColor.h"

#include <array>
#include <cstdint>
#include <vector>

namespace llcv::capture {

enum class PixelFormat { Nv12, Yuyv, Mjpeg, Bgr24, P010 };
enum class FrameLayout { Nv12, P010, Yuyv, Yuv3Plane, Bgr24, Rgb24 };

struct FramePlane {
    std::vector<uint8_t> data;
    int stride = 0;
    int width = 0;
    int height = 0;
};

struct VideoFrame {
    FrameLayout layout = FrameLayout::Nv12;
    PixelFormat sourceFormat = PixelFormat::Nv12;
    int width = 0;
    int height = 0;
    int planeCount = 0;
    std::array<FramePlane, 3> planes;
    video::ColorSpec color;
    uint64_t captureNs = 0;
    uint64_t dequeueNs = 0;
    uint64_t readyNs = 0;
    uint32_t sequence = 0;
};

void CopyPlane(FramePlane& plane, const uint8_t* source, int sourceStride, int rowBytes,
               int rows, int texelWidth);
void FillPlane(FramePlane& plane, int width, int height, uint8_t value);

}
