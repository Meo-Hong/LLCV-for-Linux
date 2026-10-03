#pragma once
#include <windows.h>

namespace llcv::ui {
// Application chrome palette. In-video UI borrows the neutral dark colors
// through OverlayStyle; video/HDR conversion and luminance remain separate.
struct Palette {
    COLORREF kBackground = RGB(22,22,22);
    COLORREF kSidebar = RGB(17,17,17);
    COLORREF kCard = RGB(29,29,29);
    COLORREF kControl = RGB(39,39,39);
    COLORREF kControlDisabled = RGB(31,31,31);
    COLORREF kHover = RGB(51,51,51);
    COLORREF kEdge = RGB(66,66,66);
    COLORREF kCardEdge = RGB(47,47,47);
    COLORREF kSelected = RGB(48,48,48);
    COLORREF kText = RGB(235,235,235);
    COLORREF kSecondary = RGB(176,176,176);
    COLORREF kDisabled = RGB(133,133,133);
    COLORREF kAccent = RGB(225,225,225);
    COLORREF kHoverEdge = RGB(101,101,101);
    COLORREF kPrimaryHover = RGB(242,242,242);
    COLORREF kPrimaryPressed = RGB(195,195,195);
    COLORREF kOnAccent = RGB(22,22,22);
    COLORREF kWarning = RGB(226,176,117);
    COLORREF kDanger = RGB(237,98,84);
};
constexpr Palette PaletteForTheme(bool light) {
    if (!light) return {};
    Palette p;
    p.kBackground = RGB(247,247,247);
    p.kSidebar = RGB(240,240,240);
    p.kCard = RGB(242,242,242);
    p.kControl = RGB(255,255,255);
    p.kControlDisabled = RGB(235,235,235);
    p.kHover = RGB(230,230,230);
    p.kEdge = RGB(180,180,180);
    p.kCardEdge = RGB(215,215,215);
    p.kSelected = RGB(225,235,252);
    p.kText = RGB(49,64,82);
    p.kSecondary = RGB(72,88,106);
    p.kDisabled = RGB(118,132,148);
    p.kAccent = RGB(37,99,205);
    p.kHoverEdge = RGB(73,123,207);
    p.kPrimaryHover = RGB(29,82,182);
    p.kPrimaryPressed = RGB(25,67,149);
    p.kOnAccent = RGB(255,255,255);
    p.kWarning = RGB(143,87,16);
    p.kDanger = RGB(187,43,43);
    return p;
}
inline bool HighContrastEnabled() {
    HIGHCONTRASTW hc{sizeof(hc)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0) &&
        (hc.dwFlags & HCF_HIGHCONTRASTON);
}
inline Palette ResolvePalette(bool light, bool highContrast) {
    auto p = PaletteForTheme(light);
    if (highContrast) {
        p.kBackground = p.kSidebar = p.kCard = p.kControl = GetSysColor(COLOR_WINDOW);
        p.kText = p.kSecondary = p.kEdge = p.kCardEdge = GetSysColor(COLOR_WINDOWTEXT);
        p.kControlDisabled = GetSysColor(COLOR_WINDOW);
        p.kDisabled = GetSysColor(COLOR_GRAYTEXT);
        p.kHover = p.kSelected = GetSysColor(COLOR_WINDOW);
        p.kAccent = p.kHoverEdge = p.kPrimaryHover = p.kPrimaryPressed = GetSysColor(COLOR_HIGHLIGHT);
        p.kOnAccent = GetSysColor(COLOR_HIGHLIGHTTEXT);
        p.kWarning = p.kDanger = GetSysColor(COLOR_WINDOWTEXT);
    }
    return p;
}
} // namespace llcv::ui
