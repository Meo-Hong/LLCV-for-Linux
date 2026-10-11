#pragma once
#include <windows.h>

namespace llcv::dark_palette {
// Shared by the audio-only screen and the F1 guide. SDR UI colors only.
inline constexpr COLORREF background = RGB(12, 15, 19);
inline constexpr COLORREF card = RGB(23, 28, 34);
inline constexpr COLORREF raised = RGB(30, 36, 44);
inline constexpr COLORREF text = RGB(237, 242, 245);
inline constexpr COLORREF secondary = RGB(162, 178, 188);
inline constexpr COLORREF accent = RGB(129, 206, 186);
inline constexpr COLORREF cardEdge = RGB(48, 58, 67);
}
