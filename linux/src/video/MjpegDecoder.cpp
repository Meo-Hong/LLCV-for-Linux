#include "video/MjpegDecoder.h"

#include <turbojpeg.h>

namespace llcv::video {
namespace {

bool Succeeded(int result, tjhandle handle) {
    return result == 0 || tjGetErrorCode(handle) == TJERR_WARNING;
}

void Reserve(capture::FramePlane& plane, int width, int height) {
    plane.stride = width;
    plane.width = width;
    plane.height = height;
    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (plane.data.size() < total) plane.data.resize(total);
}

}

MjpegDecoder::MjpegDecoder() : handle_(tjInitDecompress()) {}

MjpegDecoder::~MjpegDecoder() {
    if (handle_) tjDestroy(static_cast<tjhandle>(handle_));
}

bool MjpegDecoder::Decode(const uint8_t* data, size_t size, capture::VideoFrame& frame) {
    auto* handle = static_cast<tjhandle>(handle_);
    if (!handle) {
        lastError_ = "TurboJPEG is unavailable";
        return false;
    }
    int width = 0;
    int height = 0;
    int subsampling = 0;
    int colorspace = 0;
    if (tjDecompressHeader3(handle, data, static_cast<unsigned long>(size), &width, &height,
                            &subsampling, &colorspace) != 0) {
        lastError_ = tjGetErrorStr2(handle);
        return false;
    }
    frame.width = width;
    frame.height = height;

    const bool yuv = colorspace == TJCS_YCbCr || colorspace == TJCS_GRAY;
    if (yuv && subsampling == TJSAMP_GRAY) {
        frame.layout = capture::FrameLayout::Yuv3Plane;
        frame.planeCount = 3;
        Reserve(frame.planes[0], tjPlaneWidth(0, width, subsampling), tjPlaneHeight(0, height, subsampling));
        capture::FillPlane(frame.planes[1], 1, 1, 128);
        capture::FillPlane(frame.planes[2], 1, 1, 128);
        unsigned char* planes[3] = {frame.planes[0].data.data(), nullptr, nullptr};
        int strides[3] = {frame.planes[0].stride, 0, 0};
        const int result = tjDecompressToYUVPlanes(handle, data, static_cast<unsigned long>(size),
                                                   planes, width, strides, height, 0);
        if (!Succeeded(result, handle)) {
            lastError_ = tjGetErrorStr2(handle);
            return false;
        }
        return true;
    }

    if (yuv) {
        frame.layout = capture::FrameLayout::Yuv3Plane;
        frame.planeCount = 3;
        unsigned char* planes[3]{};
        int strides[3]{};
        for (int component = 0; component < 3; ++component) {
            auto& plane = frame.planes[component];
            Reserve(plane, tjPlaneWidth(component, width, subsampling),
                    tjPlaneHeight(component, height, subsampling));
            planes[component] = plane.data.data();
            strides[component] = plane.stride;
        }
        const int result = tjDecompressToYUVPlanes(handle, data, static_cast<unsigned long>(size),
                                                   planes, width, strides, height, 0);
        if (!Succeeded(result, handle)) {
            lastError_ = tjGetErrorStr2(handle);
            return false;
        }
        return true;
    }

    frame.layout = capture::FrameLayout::Rgb24;
    frame.planeCount = 1;
    auto& plane = frame.planes[0];
    plane.stride = width * 3;
    plane.width = width;
    plane.height = height;
    const size_t total = static_cast<size_t>(plane.stride) * static_cast<size_t>(height);
    if (plane.data.size() < total) plane.data.resize(total);
    const int result = tjDecompress2(handle, data, static_cast<unsigned long>(size), plane.data.data(),
                                     width, plane.stride, height, TJPF_RGB, 0);
    if (!Succeeded(result, handle)) {
        lastError_ = tjGetErrorStr2(handle);
        return false;
    }
    return true;
}

}
