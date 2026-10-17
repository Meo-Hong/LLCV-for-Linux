#pragma once

#include "settings/AppSettings.h"
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace llcv::video_placement {

// Called only when configuring/resizing output, never for each video frame.
// Windowed playback still fits the client. Fullscreen keeps the selected
// display size within the monitor without cropping or changing display modes.
inline RECT Destination(int sourceWidth, int sourceHeight, int outputWidth, int outputHeight,
                        bool fullscreen, int displayWidth, int displayHeight,
                        settings::FullscreenVideoPosition position) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || outputWidth <= 0 || outputHeight <= 0)
        return {};
    const int limitWidth = fullscreen ? (std::min)(outputWidth, (std::max)(1, displayWidth)) : outputWidth;
    const int limitHeight = fullscreen ? (std::min)(outputHeight, (std::max)(1, displayHeight)) : outputHeight;
    const double scale = (std::min)(static_cast<double>(limitWidth) / sourceWidth,
                                   static_cast<double>(limitHeight) / sourceHeight);
    const LONG width = std::clamp(static_cast<LONG>(std::lround(sourceWidth * scale)), 1L, static_cast<LONG>(limitWidth));
    const LONG height = std::clamp(static_cast<LONG>(std::lround(sourceHeight * scale)), 1L, static_cast<LONG>(limitHeight));
    int horizontal = 1, vertical = 1;
    if (fullscreen) {
        using P = settings::FullscreenVideoPosition;
        switch (position) {
        case P::TopLeft: horizontal = 0; vertical = 0; break;
        case P::Top: vertical = 0; break;
        case P::TopRight: horizontal = 2; vertical = 0; break;
        case P::Left: horizontal = 0; break;
        case P::Right: horizontal = 2; break;
        case P::BottomLeft: horizontal = 0; vertical = 2; break;
        case P::Bottom: vertical = 2; break;
        case P::BottomRight: horizontal = 2; vertical = 2; break;
        default: break;
        }
    }
    const LONG left = static_cast<LONG>((outputWidth - width) * static_cast<int64_t>(horizontal) / 2);
    const LONG top = static_cast<LONG>((outputHeight - height) * static_cast<int64_t>(vertical) / 2);
    return {left, top, left + width, top + height};
}
} // namespace llcv::video_placement
