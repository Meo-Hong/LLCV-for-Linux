#include "capture/VideoFrame.h"

#include <algorithm>
#include <cstring>

namespace llcv::capture {

void CopyPlane(FramePlane& plane, const uint8_t* source, int sourceStride, int rowBytes,
               int rows, int texelWidth) {
    plane.stride = rowBytes;
    plane.width = texelWidth;
    plane.height = rows;
    const size_t total = static_cast<size_t>(rowBytes) * static_cast<size_t>(rows);
    if (plane.data.size() < total) plane.data.resize(total);
    if (sourceStride == rowBytes) {
        std::memcpy(plane.data.data(), source, total);
        return;
    }
    for (int row = 0; row < rows; ++row) {
        std::memcpy(plane.data.data() + static_cast<size_t>(row) * rowBytes,
                    source + static_cast<size_t>(row) * sourceStride,
                    static_cast<size_t>(rowBytes));
    }
}

void FillPlane(FramePlane& plane, int width, int height, uint8_t value) {
    plane.stride = width;
    plane.width = width;
    plane.height = height;
    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (plane.data.size() < total) plane.data.resize(total);
    std::fill_n(plane.data.begin(), total, value);
}

}
