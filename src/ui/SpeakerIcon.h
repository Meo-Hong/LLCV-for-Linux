#pragma once

namespace llcv::ui {
inline constexpr int kSpeakerIconSize = 24;
// Shared 24-unit outline for the GDI audio window and the D2D video overlay.
// Explicit strokes avoid font-dependent speaker/emoji rendering.
template<class Line> void DrawSpeakerIcon(bool muted, Line line) {
    line(2,9,6,9); line(6,9,11,5); line(11,5,11,19);
    line(11,19,6,15); line(6,15,2,15); line(2,15,2,9);
    if (muted) {
        line(15,9,21,15); line(15,15,21,9);
    } else {
        line(15,8,17,12); line(17,12,15,16);
        line(19,5,22,12); line(22,12,19,19);
    }
}
}
