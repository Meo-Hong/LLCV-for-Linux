#include "ui/AudioOnlyView.h"
#include "ui/AppPalette.h"
#include "ui/SettingsFonts.h"
#include "ui/SpeakerIcon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>

namespace llcv::audio_only_view {
namespace {
Rect ScaleRect(const Rect& content, const Rect& logical) noexcept {
    const int width = content.right - content.left;
    const int height = content.bottom - content.top;
    return {content.left + MulDiv(logical.left, width, kBaseClientWidth),
            content.top + MulDiv(logical.top, height, kBaseClientHeight),
            content.left + MulDiv(logical.right, width, kBaseClientWidth),
            content.top + MulDiv(logical.bottom, height, kBaseClientHeight)};
}

bool Contains(const Rect& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right &&
           y >= rect.top && y < rect.bottom;
}
}  // namespace

Size MinimumClientSize(UINT dpi) noexcept {
    const int scale = static_cast<int>(std::clamp(dpi, 96u, 768u));
    return {MulDiv(kBaseClientWidth, scale, 96),
            MulDiv(kBaseClientHeight, scale, 96)};
}

TextSizes TextSizesForContent(int contentHeight) noexcept {
    // The DPI-aware minimum window keeps text legible. Scale with the cards
    // even below that minimum (e.g. a work-area clamp) to avoid overlapping.
    const auto size = [&](int normal) {
        return (std::max)(1, MulDiv(normal, (std::max)(1, contentHeight),
                                   kBaseClientHeight));
    };
    return {size(32), size(25), size(14), size(12)};
}

Rect ContentRect(int clientWidth, int clientHeight) noexcept {
    if (clientWidth <= 0 || clientHeight <= 0) return {};
    const bool widthLimited =
        static_cast<int64_t>(clientWidth) * kBaseClientHeight <=
        static_cast<int64_t>(clientHeight) * kBaseClientWidth;
    const int scaleNumerator = widthLimited ? clientWidth : clientHeight;
    const int scaleDenominator = widthLimited ? kBaseClientWidth
                                              : kBaseClientHeight;
    const int width = static_cast<int>(
        (static_cast<int64_t>(kBaseClientWidth) * scaleNumerator +
         scaleDenominator / 2) / scaleDenominator);
    const int height = static_cast<int>(
        (static_cast<int64_t>(kBaseClientHeight) * scaleNumerator +
         scaleDenominator / 2) / scaleDenominator);
    const int left = (clientWidth - width) / 2;
    const int top = (clientHeight - height) / 2;
    return {left, top, left + width, top + height};
}

audio_osd::HitTarget HitTest(int clientWidth, int clientHeight,
                             int clientX, int clientY) noexcept {
    const Rect rect = ContentRect(clientWidth, clientHeight);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0 || clientX < rect.left ||
        clientX >= rect.right || clientY < rect.top || clientY >= rect.bottom) {
        return audio_osd::HitTarget::Outside;
    }
    if (Contains(ScaleRect(rect, kLeft), clientX, clientY))
        return audio_osd::HitTarget::Left;
    if (Contains(ScaleRect(rect, kRight), clientX, clientY))
        return audio_osd::HitTarget::Right;
    if (Contains(ScaleRect(rect, kMute), clientX, clientY))
        return audio_osd::HitTarget::Mute;
    if (Contains(ScaleRect(rect, kMaster), clientX, clientY))
        return audio_osd::HitTarget::Master;
    return audio_osd::HitTarget::Panel;
}

Size FitClientSize(int desiredWidth, int maximumWidth,
                   int maximumHeight) noexcept {
    if (desiredWidth <= 0 || maximumWidth <= 0 || maximumHeight <= 0)
        return {};
    int width = (std::min)(desiredWidth, maximumWidth);
    int height = static_cast<int>(
        (static_cast<int64_t>(width) * kBaseClientHeight +
         kBaseClientWidth / 2) / kBaseClientWidth);
    if (height > maximumHeight) {
        width = static_cast<int>(
            (static_cast<int64_t>(maximumHeight) * kBaseClientWidth) /
            kBaseClientHeight);
        width = (std::max)(1, width);
        height = static_cast<int>(
            (static_cast<int64_t>(width) * kBaseClientHeight +
             kBaseClientWidth / 2) / kBaseClientWidth);
    }
    return {width, height};
}

void Paint(HDC dc, const Rect& content, const State& state) {
    if (!dc) return;
    const int width = content.right - content.left;
    const int height = content.bottom - content.top;
    if (width <= 0 || height <= 0) return;
    const int saved = SaveDC(dc);
    if (saved == 0) return;
    const auto x = [&](int value) {
        return content.left + MulDiv(value, width, kBaseClientWidth);
    };
    const auto y = [&](int value) {
        return content.top + MulDiv(value, height, kBaseClientHeight);
    };
    const auto rect = [&](int left, int top, int right, int bottom) {
        return RECT{x(left), y(top), x(right), y(bottom)};
    };

    const auto palette = ui::ResolvePalette(state.lightTheme, state.highContrast);
    const COLORREF backgroundColor = palette.kBackground;
    const COLORREF cardColor = palette.kCard;
    const COLORREF masterColor = palette.kControl;
    const COLORREF hoverColor = palette.kHover;
    const COLORREF textColor = palette.kText;
    const COLORREF secondaryColor = palette.kSecondary;
    const COLORREF accentColor = palette.kAccent;
    const COLORREF trackColor = palette.kEdge;
    const COLORREF clipColor = palette.kDanger;
    const COLORREF boostColor = palette.kWarning;

    HBRUSH background = CreateSolidBrush(backgroundColor);
    HBRUSH cardBrush = CreateSolidBrush(cardColor);
    HBRUSH hover = CreateSolidBrush(hoverColor);
    HBRUSH accent = CreateSolidBrush(accentColor);
    HBRUSH track = CreateSolidBrush(trackColor);
    HBRUSH master = CreateSolidBrush(masterColor);
    HBRUSH clipping = CreateSolidBrush(state.clipping ? clipColor : accentColor);
    HBRUSH outerEdge = CreateSolidBrush(palette.kEdge);
    HPEN cardEdge = CreatePen(PS_SOLID, 1, palette.kCardEdge);
    HPEN hoverEdge = CreatePen(PS_SOLID, 1, palette.kHoverEdge);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, GetStockObject(NULL_PEN));

    const RECT backgroundRect{content.left, content.top,
                              content.right, content.bottom};
    FillRect(dc, &backgroundRect, background);
    FrameRect(dc, &backgroundRect, outerEdge);

    const auto font = [&](int pixelHeight, int weight) {
        LOGFONTW descriptor{};
        descriptor.lfHeight = -(std::max)(1, pixelHeight);
        descriptor.lfWeight = weight;
        descriptor.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(descriptor.lfFaceName, settings_ui::SettingsFontFamily(weight, state.english));
        return CreateFontIndirectW(&descriptor);
    };
    const auto textSizes = TextSizesForContent(height);
    HFONT masterFont = font(textSizes.master, FW_MEDIUM);
    HFONT numberFont = font(textSizes.channel, FW_MEDIUM);
    HFONT bodyFont = font(textSizes.body, FW_MEDIUM);
    HFONT smallFont = font(textSizes.secondary, FW_NORMAL);
    HGDIOBJ oldFont = SelectObject(dc, bodyFont);
    const auto label = [&](const wchar_t* value, int left, int top,
                           int right, int bottom, COLORREF color,
                           UINT alignment = DT_LEFT) {
        SetTextColor(dc, color);
        RECT bounds = rect(left, top, right, bottom);
        DrawTextW(dc, value, -1, &bounds,
                  alignment | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
                      DT_END_ELLIPSIS);
    };
    const auto rounded = [&](HBRUSH brush, int left, int top,
                             int right, int bottom, int radius) {
        SelectObject(dc, brush);
        RoundRect(dc, x(left), y(top), x(right), y(bottom),
                  (std::max)(1, x(radius) - x(0)),
                  (std::max)(1, y(radius) - y(0)));
    };
    const auto baselineFor = [&](HFONT selectedFont, int top, int bottom) {
        SelectObject(dc, selectedFont);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        return y(top) + (y(bottom) - y(top) - metrics.tmHeight) / 2 + metrics.tmAscent;
    };
    const auto baselineText = [&](const wchar_t* value, RECT bounds, int baseline,
                                   COLORREF color, bool rightAligned = false) {
        SetTextColor(dc, color);
        const UINT previous = SetTextAlign(dc, TA_BASELINE | (rightAligned ? TA_RIGHT : TA_LEFT));
        ExtTextOutW(dc, rightAligned ? bounds.right : bounds.left, baseline,
                    ETO_CLIPPED, &bounds, value, static_cast<UINT>(wcslen(value)), nullptr);
        SetTextAlign(dc, previous);
    };

    const int headerBaseline = baselineFor(bodyFont, 10, 32);
    SelectObject(dc, bodyFont);
    baselineText(state.english ? L"Audio" : L"오디오", rect(12, 8, 150, 34),
                 headerBaseline, textColor);
    SelectObject(dc, smallFont);
    baselineText(state.outputLabel ? state.outputLabel : L"", rect(152, 8, 368, 34),
                 headerBaseline, secondaryColor, true);

    const auto card = [&](const Rect& bounds, int target, HBRUSH normal) {
        SelectObject(dc, state.hoveredTarget == target ? hoverEdge : cardEdge);
        rounded(state.hoveredTarget == target ? hover : normal,
                bounds.left, bounds.top, bounds.right, bounds.bottom, 14);
        SelectObject(dc, GetStockObject(NULL_PEN));
    };
    const auto percentage = [&](int percent, int left, int top,
                                 int right, int bottom, bool isMaster) {
        wchar_t value[16]{};
        swprintf_s(value, L"%d", std::clamp(percent, 0, 200));
        const HFONT valueFont = isMaster ? masterFont : numberFont;
        const int baseline = baselineFor(valueFont, top, bottom);
        const COLORREF valueColor = isMaster && state.allowBoost && percent > 100
            ? boostColor : textColor;
        SelectObject(dc, smallFont);
        SIZE unit{};
        GetTextExtentPoint32W(dc, L"%", 1, &unit);
        RECT valueBounds = rect(left, top, right, bottom);
        valueBounds.right -= unit.cx + (std::max)(1, x(4) - x(0));
        baselineText(L"%", rect(left, top, right, bottom), baseline, secondaryColor, true);
        SelectObject(dc, valueFont);
        baselineText(value, valueBounds, baseline, valueColor, true);
    };
    card(kMaster, 3, master);
    card(kMute, static_cast<int>(audio_osd::HitTarget::Mute), cardBrush);
    HPEN speakerPen = CreatePen(PS_SOLID, (std::max)(1, y(2)-y(0)), textColor);
    SelectObject(dc, speakerPen);
    constexpr int iconLeft = kMute.left + (kMute.right - kMute.left - ui::kSpeakerIconSize) / 2;
    constexpr int iconTop = kMute.top + (kMute.bottom - kMute.top - ui::kSpeakerIconSize) / 2;
    ui::DrawSpeakerIcon(state.muted, [&](int x1,int y1,int x2,int y2) {
        MoveToEx(dc,x(iconLeft+x1),y(iconTop+y1),nullptr);
        LineTo(dc,x(iconLeft+x2),y(iconTop+y2));
    });
    SelectObject(dc, GetStockObject(NULL_PEN));
    DeleteObject(speakerPen);
    SelectObject(dc, bodyFont);
    label(state.english ? L"Master volume" : L"마스터 음량",
          kMute.right + kCardPadding, 48, 216, 69, textColor);
    SelectObject(dc, smallFont);
    label(state.muted ? (state.english ? L"Muted" : L"음소거")
                      : (state.allowBoost ? (state.english ? L"Up to 200%" : L"최대 200%")
                                          : (state.english ? L"Up to 100%" : L"최대 100%")),
          kMute.right + kCardPadding, 71, 216, 90, secondaryColor);
    percentage(state.masterPercent, 218, 48, kMaster.right - kCardPadding, 90, true);

    const auto channel = [&](int target, const wchar_t* name, int percent,
                             double peakDb, const Rect& bounds) {
        const int left = bounds.left;
        const int right = bounds.right;
        card(bounds, target, cardBrush);
        percentage(percent, left + 44, 118, right - kCardPadding, 150, false);
        SelectObject(dc, bodyFont);
        label(name, left + kCardPadding, 118, left + 40, 150, textColor);
        const double validPeak = std::isfinite(peakDb)
            ? std::clamp(peakDb, -96.0, 0.0) : -96.0;
        wchar_t peak[48]{};
        swprintf_s(peak, L"%.1f dBFS", validPeak);
        SelectObject(dc, smallFont);
        label(state.english ? L"Output level" : L"출력 레벨",
              left + 12, 156, left + 82, 174, secondaryColor);
        label(peak, left + 82, 156, right - 12, 174, textColor, DT_RIGHT);
        rounded(track, left + 12, 178, right - 12, 184, 4);
        const double level = std::clamp((validPeak + 60.0) / 60.0, 0.0, 1.0);
        const int fillRight = left + 12 + static_cast<int>(
            std::lround((right - left - 24) * level));
        if (fillRight > left + 12)
            rounded(accent, left + 12, 178, fillRight, 184, 4);
    };
    channel(1, L"L", state.leftPercent, state.leftPeakDb, kLeft);
    channel(2, L"R", state.rightPercent, state.rightPeakDb, kRight);

    SelectObject(dc, clipping);
    Ellipse(dc, x(12), y(210), x(16), y(214));
    SelectObject(dc, smallFont);
    label(state.clipping ? (state.english ? L"Clipping" : L"클리핑")
                         : (state.english ? L"No clipping" : L"클리핑 없음"),
          21, 204, 117, 222,
          state.clipping ? clipColor : secondaryColor);
    label(state.english ? L"Wheel: volume · Double-click: reset"
                        : L"휠 조절 · 두 번 클릭 초기화",
          121, 204, 368, 222, secondaryColor, DT_RIGHT);

    SelectObject(dc, oldFont);
    RestoreDC(dc, saved);
    DeleteObject(smallFont);
    DeleteObject(hoverEdge);
    DeleteObject(cardEdge);
    DeleteObject(outerEdge);
    DeleteObject(bodyFont);
    DeleteObject(numberFont);
    DeleteObject(masterFont);
    DeleteObject(clipping);
    DeleteObject(master);
    DeleteObject(track);
    DeleteObject(accent);
    DeleteObject(cardBrush);
    DeleteObject(hover);
    DeleteObject(background);
}

}  // namespace llcv::audio_only_view
