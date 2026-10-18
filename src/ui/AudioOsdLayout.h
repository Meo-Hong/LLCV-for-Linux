#pragma once

namespace llcv::audio_osd {

inline constexpr int kWidth = 336;
inline constexpr int kHeight = 196;
inline constexpr int kMargin = 16;

struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

enum class HitTarget { Outside, Left, Right, Master, Panel, Mute };
inline constexpr Rect kMuteButton{24, 34, 52, 62};
inline constexpr int kMuteIndicatorSize = 36;

Rect RectForClient(int clientWidth) noexcept;
Rect MuteIndicatorRect(int clientWidth, int clientHeight) noexcept;
HitTarget HitTest(int clientWidth, int clientHeight, int clientX,
                  int clientY) noexcept;

}  // namespace llcv::audio_osd
