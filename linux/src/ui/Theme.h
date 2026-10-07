#pragma once

#include <imgui.h>

namespace llcv::ui {

struct Palette {
    ImU32 background;
    ImU32 sidebar;
    ImU32 card;
    ImU32 control;
    ImU32 controlDisabled;
    ImU32 hover;
    ImU32 edge;
    ImU32 cardEdge;
    ImU32 selected;
    ImU32 text;
    ImU32 secondary;
    ImU32 disabled;
    ImU32 accent;
    ImU32 hoverEdge;
    ImU32 primaryHover;
    ImU32 primaryPressed;
    ImU32 onAccent;
    ImU32 warning;
    ImU32 danger;
};

struct OverlayColors {
    static constexpr ImU32 kPanel = IM_COL32(17, 17, 17, 224);
    static constexpr ImU32 kPanelEdge = IM_COL32(66, 66, 66, 255);
    static constexpr ImU32 kText = IM_COL32(235, 235, 235, 255);
    static constexpr ImU32 kSecondary = IM_COL32(176, 176, 176, 255);
    static constexpr ImU32 kMeter = IM_COL32(120, 200, 140, 255);
    static constexpr ImU32 kMeterHot = IM_COL32(226, 176, 117, 255);
    static constexpr ImU32 kDanger = IM_COL32(237, 98, 84, 255);
    static constexpr ImU32 kTrack = IM_COL32(51, 51, 51, 255);
};

struct FontSet {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
};

inline constexpr float kBaseFontSize = 15.0f;

const Palette& PaletteFor(bool light);
const Palette& CurrentPalette();
ImVec4 ToVec4(ImU32 color);
FontSet LoadFonts();
void ApplyTheme(bool light, float scale);

}
