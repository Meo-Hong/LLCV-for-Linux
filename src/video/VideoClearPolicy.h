#pragma once

#include <windows.h>
#include <dxgiformat.h>
#include <cstdint>
#include <limits>

namespace llcv::video_clear {

// Cache this decision when configuring the Video Processor. It describes the
// draw that immediately follows the optional black border clear, before OSD.
// Unknown state deliberately retains the clear.
struct State {
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    DXGI_FORMAT outputFormat = DXGI_FORMAT_UNKNOWN;
    bool hdrOutput = false;
    bool scrgbOutput = false;
    bool videoProcessorPath = false;
    bool outputTargetEnabled = false;
    RECT outputTarget{};
    std::uint32_t sourceWidth = 0;
    std::uint32_t sourceHeight = 0;
    RECT source{};
    RECT destination{};
    bool singleOpaqueVideoStream = false;
    bool opaqueOutput = false;
    bool backgroundKnown = false;
    bool backgroundIsYuv = true;
    float backgroundR = 0.0f;
    float backgroundG = 0.0f;
    float backgroundB = 0.0f;
    float backgroundA = 0.0f;
};

inline bool ValidExtent(std::uint32_t extent) {
    return extent != 0 && extent <=
        static_cast<std::uint32_t>((std::numeric_limits<LONG>::max)());
}

inline bool Inside(const RECT& rectangle, std::uint32_t width,
                   std::uint32_t height) {
    return ValidExtent(width) && ValidExtent(height) &&
        rectangle.left >= 0 && rectangle.top >= 0 &&
        rectangle.right > rectangle.left && rectangle.bottom > rectangle.top &&
        rectangle.right <= static_cast<LONG>(width) &&
        rectangle.bottom <= static_cast<LONG>(height);
}

inline bool CanSkipBorderClear(const State& state) {
    if (state.hdrOutput || state.scrgbOutput || !state.videoProcessorPath ||
        state.outputFormat != DXGI_FORMAT_B8G8R8A8_UNORM ||
        !state.singleOpaqueVideoStream || !state.opaqueOutput ||
        !state.outputTargetEnabled || !state.backgroundKnown ||
        state.backgroundIsYuv || state.backgroundR != 0.0f ||
        state.backgroundG != 0.0f || state.backgroundB != 0.0f ||
        state.backgroundA != 1.0f) return false;

    if (!Inside(state.outputTarget, state.outputWidth, state.outputHeight) ||
        state.outputTarget.left != 0 || state.outputTarget.top != 0 ||
        state.outputTarget.right != static_cast<LONG>(state.outputWidth) ||
        state.outputTarget.bottom != static_cast<LONG>(state.outputHeight)) {
        return false;
    }

    // VideoProcessorBlt must write both the entire video rectangle and its
    // opaque black background on every frame, including after hiding OSD.
    return Inside(state.source, state.sourceWidth, state.sourceHeight) &&
        Inside(state.destination, state.outputWidth, state.outputHeight);
}

} // namespace llcv::video_clear
