#include "SettingsView.h"
#include "SettingsFonts.h"
#include "PresentationModeUi.h"
#include "settings/AppSettings.h"

#include <commctrl.h>
#include <algorithm>
#include <cwchar>

namespace llcv::settings_ui {
using settings::VideoPixelFormat;

void SetSettingsText(HWND control, const wchar_t* text) {
    if (!control) return;
    wchar_t current[2048]{};
    GetWindowTextW(control, current, ARRAYSIZE(current));
    if (std::wcscmp(current, text) != 0) SetWindowTextW(control, text);
}

int SettingsPixels(int dips, UINT dpi) {
    return MulDiv(dips, dpi ? dpi : USER_DEFAULT_SCREEN_DPI,
                  USER_DEFAULT_SCREEN_DPI);
}

static constexpr int kSettingsTabbedClientHeightDip = 650;

static constexpr SettingsTab kNavigationTabs[] = {
    SettingsTab::VideoWindow, SettingsTab::Audio, SettingsTab::Window,
    SettingsTab::GuideDiagnostics, SettingsTab::Updates};

SettingsTab SettingsTabFromNavigationIndex(int index) {
    return index >= 0 && index < kSettingsNavigationCount
        ? kNavigationTabs[index] : SettingsTab::VideoWindow;
}

int SettingsNavigationIndex(SettingsTab tab) {
    for (int i = 0; i < kSettingsNavigationCount; ++i)
        if (kNavigationTabs[i] == tab) return i;
    return 0;
}

void RefreshSettingsPageHeader(SettingsControls* state) {
    if (!state) return;
    const wchar_t* title = L"";
    const wchar_t* subtitle = L"";
    switch (state->activeTab) {
    case SettingsTab::VideoWindow:
        title = state->english ? L"Video" : L"영상";
        subtitle = state->english ? L"Capture devices, video formats and display settings." : L"캡처 장치, 영상 형식과 화면 표시를 설정하세요.";
        break;
    case SettingsTab::Audio:
        title = state->english ? L"Audio" : L"오디오";
        subtitle = state->english ? L"Output devices, volume controls and playback stability." : L"출력 장치, 음량 표시와 재생 안정성을 설정하세요.";
        break;
    case SettingsTab::Window:
        title = state->english ? L"Window" : L"창";
        subtitle = state->english ? L"Make the viewer fit your desktop." : L"창 이동과 테두리, 전체화면 동작을 설정하세요.";
        break;
    case SettingsTab::GuideDiagnostics:
        title = state->english ? L"Guide & diagnostics" : L"도움말 · 진단";
        subtitle = state->english ? L"Keyboard shortcuts and diagnostic logs." : L"단축키를 확인하고 진단 로그를 관리하세요.";
        break;
    case SettingsTab::Updates:
        title = state->english ? L"App preferences" : L"앱 설정";
        subtitle = state->english ? L"Language, startup behavior and updates." : L"언어, 시작 방식과 업데이트를 관리하세요.";
        break;
    }
    SetSettingsText(state->pageTitle, title);
    SetSettingsText(state->pageSubtitle, subtitle);
}

int SettingsClientHeightDip(const SettingsControls* state) {
    (void)state;
    return kSettingsTabbedClientHeightDip;
}

SIZE SettingsDialogOuterSize(HWND hwnd, UINT dpi,
                                    const SettingsControls* state) {
    RECT rect{0, 0, SettingsPixels(kSettingsClientWidthDip, dpi),
              SettingsPixels(SettingsClientHeightDip(state), dpi)};
    const DWORD style =
        static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD exStyle =
        static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    if (!AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, dpi)) {
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    }
    return SIZE{rect.right - rect.left, rect.bottom - rect.top};
}

SettingsVisualUpdate::SettingsVisualUpdate(HWND owner) {
    if (owner && IsWindowVisible(owner)) {
        owner_ = owner;
        SendMessageW(owner_, WM_SETREDRAW, FALSE, 0);
    }
}
SettingsVisualUpdate::~SettingsVisualUpdate() {
    if (!owner_ || !IsWindow(owner_)) return;
    SendMessageW(owner_, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(owner_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void PlaceSettingsControl(HWND control, int x, int y, int width,
                                 int height, UINT dpi) {
    if (!control) return;
    RECT current{};
    GetWindowRect(control, &current);
    MapWindowPoints(HWND_DESKTOP, GetParent(control), reinterpret_cast<POINT*>(&current), 2);
    if (current.left == SettingsPixels(x, dpi) && current.top == SettingsPixels(y, dpi) &&
        current.right - current.left == SettingsPixels(width, dpi)) {
        wchar_t name[32]{};
        GetClassNameW(control, name, ARRAYSIZE(name));
        const int actualHeight = current.bottom - current.top;
        if (actualHeight == SettingsPixels(height, dpi) ||
            (_wcsicmp(name, L"COMBOBOX") == 0 && actualHeight == SettingsPixels(kSettingsComboHeightDip, dpi)))
            return;
    }
    SetWindowPos(control, nullptr, SettingsPixels(x, dpi),
                 SettingsPixels(y, dpi), SettingsPixels(width, dpi),
                 SettingsPixels(height, dpi),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ApplySettingsComboMetrics(HWND control, UINT dpi) {
    if (!control) return;
    RECT previous{};
    GetWindowRect(control, &previous);
    if (previous.bottom - previous.top == SettingsPixels(kSettingsComboHeightDip, dpi) &&
        SendMessageW(control, CB_GETITEMHEIGHT, 0, 0) == SettingsPixels(24, dpi)) return;
    // Both SetWindowPos and CB_SETDROPPEDWIDTH can restore a combo's
    // font-derived closed height. This must be the LAST layout operation.
    SendMessageW(control, CB_SETITEMHEIGHT, 0, SettingsPixels(24, dpi));
    SendMessageW(control, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                 SettingsPixels(24, dpi));
    RECT actual{};
    GetWindowRect(control, &actual);
    const int itemHeight = static_cast<int>(SendMessageW(
        control, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0));
    const int adjustment = SettingsPixels(kSettingsComboHeightDip, dpi) -
                           (actual.bottom - actual.top);
    // Native border thickness is not necessarily scaled like our DIPs.
    // Measure it rather than assuming a fixed six-pixel frame at all DPI.
    if (itemHeight != CB_ERR && adjustment != 0)
        SendMessageW(control, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                     std::max(1, itemHeight + adjustment));
}

static BOOL CALLBACK SetSettingsChildFont(HWND child, LPARAM fontValue) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(fontValue), FALSE);
    return TRUE;
}

void ApplySettingsFont(SettingsControls* state, HWND hwnd,
                              UINT dpi) {
    if (!state || !hwnd) return;
    const auto createFont = [dpi, state](int points, int weight) {
        return CreateFontW(-MulDiv(points, dpi ? dpi : USER_DEFAULT_SCREEN_DPI, 72),
            0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, SettingsFontFamily(weight, state->english));
    };
    HFONT font = createFont(kSettingsBodyFontPoints, FW_MEDIUM);
    HFONT secondaryFont = createFont(kSettingsSecondaryFontPoints, FW_NORMAL);
    HFONT sectionFont = createFont(kSettingsBodyFontPoints, FW_SEMIBOLD);
    HFONT titleFont = createFont(kSettingsTitleFontPoints, FW_SEMIBOLD);
    // Allocate the complete replacement before touching the active set. A
    // failed allocation must not leave controls pointing at deleted fonts.
    if (!font || !secondaryFont || !sectionFont || !titleFont) {
        for (HFONT candidate : {font, secondaryFont, sectionFont, titleFont})
            if (candidate) DeleteObject(candidate);
        return;
    }
    std::vector<HFONT> previousFonts;
    previousFonts.swap(state->uiFonts);
    state->uiFonts = {font, secondaryFont, sectionFont, titleFont};
    EnumChildWindows(hwnd, SetSettingsChildFont,
                     reinterpret_cast<LPARAM>(font));
    // Smaller explanatory copy recedes without making interactive options
    // harder to read. Labels, fields, navigation and buttons remain 10 pt.
    for (HWND control : {state->pageSubtitle, state->versionWatermark,
                         state->audioStatus, state->surround51Hint,
                         state->captureAudioStatus, state->videoCapabilityStatus,
                         state->vsrStatus, state->screenshotHelp,
                         state->relativeSizeWarning, state->fullscreenCursorHint,
                         state->guideDiagnosticsText, state->guideVideoHint, state->skipStartupHint,
                         state->updateText, state->updateStatus}) {
        if (control) SendMessageW(control, WM_SETFONT,
                                  reinterpret_cast<WPARAM>(secondaryFont), FALSE);
    }
    for (HWND control : {state->audioOutputSection,
                         state->audioPlaybackSection,
                         state->audioStabilitySection,
                         state->videoCaptureSection,
                         state->videoDisplaySection,
                         state->videoWindowSection,
                         state->appPreferencesSection,
                         state->brandLabel,
                         state->updateTitle,
                         state->startButton,
                         state->guideShortcutsTitle,
                         state->screenshotTitle,
                         state->guideDiagnosticsTitle}) {
        if (control) {
            SendMessageW(control, WM_SETFONT,
                         reinterpret_cast<WPARAM>(sectionFont), FALSE);
        }
    }

    for (HWND key : state->guideKeys) {
        if (key) SendMessageW(key, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont), FALSE);
    }
    if (state->pageTitle) {
        SendMessageW(state->pageTitle, WM_SETFONT,
                     reinterpret_cast<WPARAM>(titleFont), FALSE);
    }
    // Every child now references the replacement set, so monitor transitions
    // release all four prior resources instead of accumulating GDI objects.
    for (HFONT previous : previousFonts) DeleteObject(previous);
}

// Checkbox captions vary substantially between Korean and English.  Measure
// the actual current UI font so a neighbouring help button stays attached to
// its option at every DPI instead of relying on a fragile hard-coded x value.
static int SettingsCheckboxWidthDip(HWND checkbox, UINT dpi) {
    if (!checkbox) return 250;
    wchar_t text[512]{};
    GetWindowTextW(checkbox, text, ARRAYSIZE(text));
    HDC hdc = GetDC(checkbox);
    if (!hdc) return 250;
    const HFONT font = reinterpret_cast<HFONT>(
        SendMessageW(checkbox, WM_GETFONT, 0, 0));
    const HGDIOBJ oldFont = font ? SelectObject(hdc, font) : nullptr;
    SIZE size{};
    GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &size);
    if (oldFont) SelectObject(hdc, oldFont);
    ReleaseDC(checkbox, hdc);
    const int textWidthDip = MulDiv(
        size.cx, USER_DEFAULT_SCREEN_DPI,
        dpi ? dpi : USER_DEFAULT_SCREEN_DPI);
    // Checkbox glyph plus caption.  Keeping the HWND no wider than this is
    // important: a wide checkbox would overlap a nearby help button and
    // steal its clicks even when the button looks visually separate.
    return std::min(29 + textWidthDip, 430);
}

static bool WindowBehaviorWarningNeeded(const SettingsControls* state) {
    return state->pixelCheck && state->relativeSizeCheck &&
        SendMessageW(state->pixelCheck, BM_GETCHECK, 0, 0) == BST_CHECKED &&
        SendMessageW(state->relativeSizeCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void LayoutWindowBehaviorControls(SettingsControls* state) {
    const UINT dpi = state->layoutDpi;
    const auto place = [dpi](HWND control, int x, int y, int width, int height) {
        PlaceSettingsControl(control, x, y, width, height, dpi);
    };
    // A dedicated note row prevents controls from jumping under the pointer
    // when Pixel-perfect/relative sizing changes.
    place(state->videoWindowSection, 184, 112, 792, 20);
    place(state->relativeSizeCheck, 184, 140, 760, 32);
    place(state->relativeSizeWarning, 184, 428, 792, 56);
    place(state->borderlessCheck, 184, 180, 760, 32);
    place(state->roundedCornersCheck, 184, 220, 760, 32);
    place(state->windowSnapCheck, 184, 260, 760, 32);
    place(state->fullscreenCursorLabel, 184, 316, 376, 20);
    place(state->fullscreenCursorCombo, 184, 340, 376, 120);
    place(state->fullscreenCursorHint, 184, 380, 376, 24);
    if (state->fullscreenCursorHint)
        SetWindowPos(state->fullscreenCursorHint, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ApplySettingsComboMetrics(state->fullscreenCursorCombo, dpi);
}

void LayoutSettingsControls(SettingsControls* state, UINT dpi) {
    if (!state) return;
    state->layoutDpi = dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
    const auto place = [dpi](HWND control, int x, int y, int width, int height) {
        PlaceSettingsControl(control, x, y, width, height, dpi);
    };
    // The navigation is deliberately narrow. Labels sit above fields, allowing
    // full-width device names without making the dialog wider.
    place(state->brandLabel, 10, 28, 132, 40);
    place(state->tabControl, 10, 100, 132, 220);
    SendMessageW(state->tabControl, LB_SETITEMHEIGHT, 0, SettingsPixels(44, dpi));
    place(state->pageTitle, 184, 24, 792, 40);
    place(state->pageSubtitle, 184, 66, 792, 24);
    place(state->versionWatermark, 10, 608, 132, 20);
    place(state->startButton, 762, 602, 102, 34);
    place(state->cancelButton, 876, 602, 100, 34);

    // Labels share a 64-DIP cadence, with a four-DIP label/field gap. Related
    // checkboxes stay grouped; the lower timing section gets a 24-DIP break.
    place(state->audioOutputSection, 184, 112, 376, 20);
    place(state->audioLabel, 184, 140, 376, 20);
    place(state->audioCombo, 184, 164, 376, 160);
    place(state->audioOutputLabel, 184, 204, 376, 20);
    place(state->audioOutputCombo, 184, 228, 376, 220);
    place(state->bufferLabel, 184, 268, 376, 20);
    place(state->bufferCombo, 184, 292, 376, 180);
    place(state->audioStatus, 184, 332, 376, 48);
    place(state->exclusiveTestButton, 184, 388, 185, 32);
    place(state->audioPlaybackSection, 600, 112, 376, 20);
    place(state->volumeHudLabel, 600, 140, 376, 20);
    place(state->volumeHudCombo, 600, 164, 376, 160);
    const int boostWidth = std::min(SettingsCheckboxWidthDip(state->volumeBoostCheck, dpi), 344);
    place(state->volumeBoostCheck, 600, 204, boostWidth, 32);
    place(state->volumeBoostHelp, 600 + boostWidth + 8, 208, 24, 24);
    place(state->muteBackgroundCheck, 600, 244, 376, 32);
    place(state->audioOnlyCheck, 600, 284, 376, 32);
    place(state->surround51Check, 600, 336, 376, 32);
    place(state->surround51Hint, 600, 376, 376, 88);
    place(state->audioStabilitySection, 184, 488, 792, 20);
    place(state->driftLabel, 184, 520, 300, 20);
    place(state->driftHelp, 536, 518, 24, 24);
    place(state->driftCombo, 184, 544, 376, 120);
    place(state->pcmQueueLabel, 600, 520, 300, 20);
    place(state->pcmQueueHelp, 952, 518, 24, 24);
    place(state->pcmQueueCombo, 600, 544, 376, 140);

    // Video: preserve the detected-format summary instead of hiding capability
    // information behind another interaction.
    place(state->videoCaptureSection, 184, 112, 268, 20);
    place(state->videoRefreshButton, 476, 108, 84, 28);
    place(state->captureDeviceLabel, 184, 140, 376, 20);
    place(state->captureDeviceCombo, 184, 164, 376, 220);
    place(state->captureAudioDeviceLabel, 184, 204, 376, 20);
    place(state->captureAudioDeviceCombo, 184, 228, 376, 220);
    place(state->captureAudioStatus, 184, 228, 376, 30);
    place(state->videoLabel, 184, 268, 180, 20);
    place(state->videoCombo, 184, 292, 180, 120);
    place(state->frameRateLabel, 380, 268, 180, 20);
    place(state->frameRateCombo, 380, 292, 180, 200);
    SendMessageW(state->frameRateCombo, CB_SETDROPPEDWIDTH, SettingsPixels(342, dpi), 0);
    SendMessageW(state->captureDeviceCombo, CB_SETDROPPEDWIDTH, SettingsPixels(560, dpi), 0);
    SendMessageW(state->captureAudioDeviceCombo, CB_SETDROPPEDWIDTH, SettingsPixels(560, dpi), 0);
    SendMessageW(state->audioOutputCombo, CB_SETDROPPEDWIDTH, SettingsPixels(560, dpi), 0);
    place(state->pixelFormatLabel, 184, 332, 376, 20);
    place(state->pixelFormatCombo, 184, 356, 376, 160);
    place(state->videoCapabilityStatus, 184, 392, 376, 84);
    place(state->forceHdr10Check, 184, 480, 344, 40);
    place(state->forceHdr10Help, 536, 488, 24, 24);
    place(state->hdrChromaLabel, 184, 528, 322, 20);
    place(state->hdrChromaCombo, 184, 552, 344, 150);
    place(state->hdrChromaHelp, 536, 555, 24, 24);
    place(state->mjpegColorLabel, 184, 488, 322, 20);
    place(state->mjpegColorCombo, 184, 512, 344, 150);
    place(state->mjpegColorHelp, 536, 515, 24, 24);

    place(state->videoDisplaySection, 600, 112, 376, 20);
    place(state->presentationLabel, 600, 140, 300, 20);
    place(state->presentationHelp, 952, 138, 24, 24);
    place(state->presentationCombo, 600, 164, 376, 120);
    place(state->displayMonitorLabel, 600, 204, 376, 20);
    place(state->displayMonitorCombo, 600, 228, 376, 180);
    SendMessageW(state->displayMonitorCombo, CB_SETDROPPEDWIDTH, SettingsPixels(560, dpi), 0);
    place(state->pixelCheck, 600, 332, 376, 32);
    place(state->scalingLabel, 600, 268, 376, 20);
    place(state->scalingCombo, 600, 292, 376, 120);
    // Keep equal eight-DIP gaps between cards and above the footer, without
    // changing the dialog size or shrinking interactive targets.
    place(state->vsrCheck, 600, 376, 264, 32);
    place(state->vsrGuideButton, 876, 376, 100, 32);
    place(state->vsrCaptureLabel, 600, 412, 172, 30);
    place(state->vsrCaptureCombo, 786, 412, 190, 150);
    place(state->vsrStatus, 600, 448, 376, 20);
    place(state->screenshotTitle, 600, 492, 376, 20);
    place(state->screenshotClipboardCheck, 600, 516, 182, 24);
    place(state->screenshotFolderButton, 600, 544, 182, 28);
    place(state->screenshotHelp, 794, 542, 182, 32);

    // Window behavior has its own page, avoiding a packed video/options wall.
    LayoutWindowBehaviorControls(state);

    place(state->guideShortcutsTitle, 204, 132, 336, 20);
    for (size_t i = 0; i < state->guideKeys.size(); ++i) {
        const int y = 168 + static_cast<int>(i) * 36;
        place(state->guideKeys[i], 214, y + 3, 40, 23);
        place(state->guideDescriptions[i], 276, y, 264, 29);
    }
    place(state->guideVideoHint, 204, 501, 336, 36);
    place(state->guideDiagnosticsTitle, 600, 132, 376, 20);
    place(state->guideDiagnosticsText, 600, 160, 376, 32);
    place(state->saveLogCheck, 600, 208, 376, 32);
    place(state->showConsoleCheck, 600, 248, 376, 32);
    place(state->guideLogFolderButton, 600, 296, 185, 32);

    // App-wide preferences no longer dominate every page.
    place(state->appPreferencesSection, 184, 112, 792, 20);
    place(state->languageLabel, 184, 140, 376, 20);
    place(state->languageCombo, 184, 164, 376, 120);
    place(state->themeLabel, 600, 140, 376, 20);
    place(state->themeCombo, 600, 164, 376, 120);
    place(state->skipStartupCheck, 184, 212, 760, 32);
    place(state->skipStartupHint, 212, 252, 732, 28);
    place(state->updateTitle, 184, 312, 792, 24);
    place(state->updateText, 184, 344, 730, 56);
    place(state->checkForUpdatesCheck, 184, 412, 760, 32);
    place(state->updateNowButton, 184, 456, 185, 32);
    place(state->updateStatus, 184, 500, 792, 48);
    for (HWND combo : {state->audioCombo, state->audioOutputCombo,
                        state->bufferCombo, state->volumeHudCombo,
                        state->driftCombo, state->pcmQueueCombo,
                        state->captureDeviceCombo, state->captureAudioDeviceCombo,
                        state->videoCombo, state->vsrCaptureCombo, state->frameRateCombo,
                        state->pixelFormatCombo, state->presentationCombo,
                        state->displayMonitorCombo, state->scalingCombo,
                        state->hdrChromaCombo, state->mjpegColorCombo,
                        state->fullscreenCursorCombo, state->languageCombo, state->themeCombo})
        ApplySettingsComboMetrics(combo, dpi);
}

void SetSettingsControlVisible(HWND control, bool visible, bool enabled) {
    if (!control) return;
    const bool wasVisible = (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0;
    if (wasVisible != visible) ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    const bool shouldEnable = visible && enabled;
    if ((IsWindowEnabled(control) != FALSE) != shouldEnable) EnableWindow(control, shouldEnable);
}

void UpdateScalingControlVisibility(SettingsControls* state) {
    if (!state) return;
    const bool pixelPerfect = state->pixelCheck &&
        SendMessageW(state->pixelCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool vsr = SendMessageW(state->vsrCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool visible = state->activeTab == SettingsTab::VideoWindow;
    SetSettingsControlVisible(state->scalingLabel, visible, !pixelPerfect || vsr);
    SetSettingsControlVisible(state->scalingCombo, visible, !pixelPerfect || vsr);
}

void UpdateWindowBehaviorVisibility(SettingsControls* state) {
    if (!state) return;

    // These are everyday window-behavior preferences, not advanced tuning.
    // Only show the caveat when the currently selected combination needs it.
    const bool visible = state->activeTab == SettingsTab::Window;
    SetSettingsControlVisible(state->videoWindowSection, visible);
    SetSettingsControlVisible(state->windowSnapCheck, visible);
    SetSettingsControlVisible(state->relativeSizeCheck, visible);
    SetSettingsControlVisible(state->borderlessCheck, visible);
    SetSettingsControlVisible(state->roundedCornersCheck, visible);
    SetSettingsControlVisible(state->fullscreenCursorLabel, visible);
    SetSettingsControlVisible(state->fullscreenCursorCombo, visible);
    SetSettingsControlVisible(state->fullscreenCursorHint, visible);
    SetSettingsControlVisible(state->relativeSizeWarning,
                              visible && WindowBehaviorWarningNeeded(state));
}

void UpdateAdvancedControlVisibility(SettingsControls* state, bool exclusive,
                                            settings::VideoPixelFormat selectedFormat,
                                            SettingsAvailability availability) {
    if (!state) return;
    const bool audio = state->activeTab == SettingsTab::Audio;
    const bool video = state->activeTab == SettingsTab::VideoWindow;
    const bool guide = state->activeTab == SettingsTab::GuideDiagnostics;
    const bool updates = state->activeTab == SettingsTab::Updates;
    RefreshSettingsPageHeader(state);
    for (HWND control : {state->tabControl, state->brandLabel,
                         state->pageTitle, state->pageSubtitle,
                         state->versionWatermark,
                         state->cancelButton}) {
        SetSettingsControlVisible(control, true);
    }
    SetSettingsControlVisible(state->startButton, true, availability.start);
    for (HWND control : {state->languageLabel, state->languageCombo,
                         state->themeLabel, state->themeCombo,
                         state->skipStartupCheck, state->skipStartupHint,
                         state->appPreferencesSection})
        SetSettingsControlVisible(control, updates);
    for (HWND control : {state->audioOutputSection,
                         state->audioPlaybackSection,
                         state->audioStabilitySection,
                         state->audioLabel, state->audioCombo,
                         state->audioOutputLabel, state->audioOutputCombo,
                         state->bufferLabel,
                         state->audioStatus,
                         state->volumeHudLabel, state->volumeHudCombo,
                         state->volumeBoostCheck, state->volumeBoostHelp,
                         state->muteBackgroundCheck, state->audioOnlyCheck,
                         state->driftLabel, state->driftHelp, state->driftCombo,
                         state->pcmQueueLabel, state->pcmQueueHelp,
                         state->pcmQueueCombo}) {
        SetSettingsControlVisible(control, audio);
    }
    SetSettingsControlVisible(state->bufferCombo, audio, availability.audioBuffer);
    // The endpoint recheck belongs only to WASAPI Exclusive.  In Shared and
    // ASIO modes it is both irrelevant and misleading, even on the Audio tab.
    SetSettingsControlVisible(state->exclusiveTestButton,
                              audio && exclusive, availability.exclusiveProbe);
    const bool shared = SendMessageW(state->audioCombo, CB_GETCURSEL, 0, 0) == 0;
    SetSettingsControlVisible(state->surround51Check, audio, shared);
    SetSettingsControlVisible(state->surround51Hint, audio, shared);
    for (HWND control : {state->videoCaptureSection,
                         state->videoRefreshButton,
                         state->videoDisplaySection,
                         state->presentationLabel, state->presentationHelp,
                         state->presentationCombo, state->displayMonitorLabel,
                         state->displayMonitorCombo, state->captureDeviceLabel,
                         state->captureAudioDeviceLabel, state->videoLabel,
                         state->videoCombo, state->pixelFormatLabel,
                         state->frameRateLabel, state->videoCapabilityStatus,
                         state->pixelCheck}) {
        SetSettingsControlVisible(control, video);
    }
    SetSettingsControlVisible(state->captureDeviceCombo, video, availability.captureDevice);
    SetSettingsControlVisible(state->pixelFormatCombo, video, availability.formats);
    SetSettingsControlVisible(state->frameRateCombo, video, availability.formats);
    // This row has two mutually exclusive controls: the device picker for a
    // separate capture endpoint, or the short "built-in audio" status. Keep
    // its existing video-tab choice intact; hide both together off-tab.
    if (!video) {
        SetSettingsControlVisible(state->captureAudioDeviceCombo, false);
        SetSettingsControlVisible(state->captureAudioStatus, false);
    }
    const bool p010Selected = selectedFormat ==
        VideoPixelFormat::P010;
    const bool mjpegSelected = selectedFormat ==
        VideoPixelFormat::Mjpeg;
    SetSettingsControlVisible(state->forceHdr10Check, video && p010Selected);
    SetSettingsControlVisible(state->forceHdr10Help, video && p010Selected);
    SetSettingsControlVisible(state->hdrChromaLabel, video && p010Selected);
    SetSettingsControlVisible(state->hdrChromaCombo, video && p010Selected);
    SetSettingsControlVisible(state->hdrChromaHelp, video && p010Selected);
    SetSettingsControlVisible(state->mjpegColorLabel, video && mjpegSelected);
    SetSettingsControlVisible(state->mjpegColorCombo, video && mjpegSelected);
    SetSettingsControlVisible(state->mjpegColorHelp, video && mjpegSelected);
    SetSettingsControlVisible(state->guideShortcutsTitle, guide);
    SetSettingsControlVisible(state->guideVideoHint, guide);
    for (HWND key : state->guideKeys) SetSettingsControlVisible(key, guide);
    for (HWND description : state->guideDescriptions) SetSettingsControlVisible(description, guide);
    SetSettingsControlVisible(state->guideDiagnosticsTitle, guide);
    SetSettingsControlVisible(state->guideDiagnosticsText, guide);
    SetSettingsControlVisible(state->guideLogFolderButton, guide);
    SetSettingsControlVisible(state->screenshotTitle, video);
    if (state->vsrGpuUnavailable &&
        SendMessageW(state->vsrCheck, BM_GETCHECK, 0, 0) != BST_UNCHECKED)
        SendMessageW(state->vsrCheck, BM_SETCHECK, BST_UNCHECKED, 0);
    SetSettingsControlVisible(state->vsrCheck, video, !state->vsrGpuUnavailable);
    SetSettingsControlVisible(state->vsrGuideButton, video);
    SetSettingsControlVisible(state->vsrStatus, video);
    const bool vsr = SendMessageW(state->vsrCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SetSettingsControlVisible(state->vsrCaptureLabel, video, vsr);
    SetSettingsControlVisible(state->vsrCaptureCombo, video, vsr);
    SetSettingsText(state->videoLabel, vsr
        ? (state->english ? L"Display resolution" : L"표시 해상도")
        : (state->english ? L"Capture resolution" : L"캡처 해상도"));
    SetSettingsText(state->pixelCheck, vsr
        ? (state->english ? L"Lock display size" : L"표시 크기 고정")
        : L"Pixel-perfect (1:1)");
    SetSettingsText(state->vsrStatus, state->vsrGpuUnavailable
        ? (state->vsrGpuUnknown
            ? (state->english ? L"GPU check failed · Restart to retry" : L"GPU 확인 실패 · 앱을 다시 실행하세요")
            : (state->english ? L"Unavailable · Requires an NVIDIA rendering GPU" : L"사용 불가 · NVIDIA 렌더링 GPU가 필요합니다"))
        : (state->english ? L"SDR / HDR10 · Toggle with F6"
                          : L"SDR / HDR10 · F6으로 전환"));
    SetSettingsControlVisible(state->screenshotClipboardCheck, video);
    SetSettingsControlVisible(state->screenshotHelp, video);
    SetSettingsControlVisible(state->screenshotFolderButton, video);
    SetSettingsControlVisible(state->saveLogCheck, guide);
    SetSettingsControlVisible(state->showConsoleCheck, guide);
    SetSettingsControlVisible(state->updateTitle, updates);
    SetSettingsControlVisible(state->updateText, updates);
    SetSettingsControlVisible(state->checkForUpdatesCheck, updates);
    SetSettingsControlVisible(state->updateNowButton, updates);
    SetSettingsControlVisible(state->updateStatus, updates);
    UpdateScalingControlVisibility(state);
    UpdateWindowBehaviorVisibility(state);
}

void TrackSettingsTooltip(HWND target, HWND tooltip, bool active) {
    if (!target || !tooltip) return;
    TOOLINFOW tool{};
    tool.cbSize = TTTOOLINFO_V1_SIZE;
    tool.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
    tool.hwnd = GetParent(target);
    tool.uId = reinterpret_cast<UINT_PTR>(target);
    SendMessageW(tooltip, TTM_TRACKACTIVATE, active ? TRUE : FALSE,
                 reinterpret_cast<LPARAM>(&tool));
}

void AddSettingsTooltip(SettingsControls* state, HWND owner,
                               HWND target, const wchar_t* text) {
    if (!state || !owner || !target || !text) return;
    if (!state->tooltipWindow) {
        state->tooltipWindow = CreateWindowExW(
            WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!state->tooltipWindow) return;
        SetWindowPos(state->tooltipWindow, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(state->tooltipWindow, TTM_SETMAXTIPWIDTH, 0, 430);
        SendMessageW(state->tooltipWindow, TTM_SETDELAYTIME,
                     TTDT_INITIAL, 250);
    }
    TOOLINFOW tool{};
    // The application intentionally has no Common Controls v6 manifest.  The
    // built-in v5 tooltip rejects the newer structure size on some Windows
    // installations, so register the compatible v1 fields explicitly.
    tool.cbSize = TTTOOLINFO_V1_SIZE;
    tool.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
    tool.hwnd = owner;
    tool.uId = reinterpret_cast<UINT_PTR>(target);
    tool.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(state->tooltipWindow, TTM_ADDTOOLW, 0,
                 reinterpret_cast<LPARAM>(&tool));
}

bool IsSettingsHelpControl(const SettingsControls* state,
                                  HWND target) {
    return state && (target == state->driftHelp ||
                     target == state->pcmQueueHelp ||
                      target == state->presentationHelp ||
                      target == state->volumeBoostHelp ||
                      target == state->forceHdr10Help ||
                      target == state->hdrChromaHelp ||
                      target == state->mjpegColorHelp);
}

const wchar_t* SettingsHelpText(SettingsHelpTopic topic, bool english) {
    if (english) {
        switch (topic) {
        case SettingsHelpTopic::Drift:
            return L"Preventing audio tearing · deciding whether correction is needed\n\n"
                    L"The capture and output device clocks can run at slightly different rates. "
                    L"Auto mode watches the application PCM queue first and enables resampling only "
                    L"when a sustained imbalance is detected. It stays enabled for the rest of the "
                    L"session once triggered, avoiding repeated on/off clicks. Check the Tab OSD for "
                    L"10–30 minutes.\n\n"
                   L"'Stable · correction unnecessary' or 'Rare errors · Off can be kept' means "
                   L"you can leave it Off when the audio is clean. If 'Repeated imbalance · "
                    L"correction recommended' continues, choose Auto. Do not judge "
                   L"from errors immediately after startup.\n\n"
                   L"The resampler and PCM safety buffer are independent. 'Resampler correction "
                   L"limit approaching' indicates clock difference; 'Possible PCM buffer shortage' "
                   L"indicates a momentary lack of queued audio; 'Capture packet delay detected' "
                   L"indicates a late input callback. If the resampler is healthy but underruns "
                   L"continue, raise the PCM buffer target first.\n\n"
                   L"The imbalance ppm shown in the OSD is an estimate from accumulated underrun/"
                    L"overrun frames, not a direct hardware-clock measurement. When Auto activates, "
                    L"the resampler adds a small amount of audio buffering and changes PCM samples. "
                    L"Off always preserves the original PCM path; On always uses the resampler.";
        case SettingsHelpTopic::PcmQueue:
            return L"PCM buffer target\n\n"
                   L"The amount of captured audio kept inside the application before playback.\n"
                   L"10 ms is minimum latency, 15 ms is the low-latency target, 20 ms is a stability "
                   L"target, 25 ms is the recommended default, and 30 ms prioritizes stability.\n\n"
                   L"Higher values absorb more scheduling jitter but add the same amount of audio "
                   L"latency. This is independent of the WASAPI output buffer and clock-drift correction.";
        case SettingsHelpTopic::Presentation:
            return llcv::presentation_ui::HelpText(true);
        case SettingsHelpTopic::VolumeBoost:
            return L"Volume boost above 100%\n\n"
                   L"Allows the mouse wheel to raise the app volume up to 200%. "
                   L"100% is the original PCM level; values above it apply digital gain only inside this app.\n\n"
                   L"No audio buffer or frame queue is added, so this option does not add audio latency. "
                   L"At high source volumes, boosting can clip peaks and cause distortion. Keep it off unless "
                   L"the capture audio is genuinely too quiet.";
        case SettingsHelpTopic::ForceHdr10:
            return L"Force HDR10 output\n\n"
                   L"P010 without transfer/gamut metadata already defaults to BT.2020/PQ HDR10; "
                   L"you do not need to enable this option for missing metadata alone. Explicit SDR metadata "
                   L"is rejected when this option is off: P010 is HDR-only. Select NV12/YUY2 for SDR.\n\n"
                   L"Use this override only for confirmed PQ/BT.2020 input with incorrect or incomplete color "
                   L"metadata. Range and chroma validation still apply. It does not convert SDR into HDR. "
                   L"For an SDR source without color metadata, select NV12/YUY2 instead. Enable Windows HDR "
                   L"on the viewing monitor. No frame queue is added.";
        case SettingsHelpTopic::HdrChroma:
            return L"HDR chroma placement\n\n"
                   L"Auto follows the device metadata and rejects unsupported placements. Use Top-left or Left "
                   L"only for a P010 HDR device with missing or incorrect chroma metadata. Compare fine colored "
                   L"edges and text against a reference.\n\n"
                   L"This overrides the declared chroma placement; it does not repair genuinely staggered Cb/Cr "
                   L"planes or guarantee that placement 6 is supported. It does not change brightness, saturation, "
                   L"PQ interpretation or range validation, and adds no frame queue or processing pass. "
                   L"P010 without transfer/gamut metadata already defaults to PQ/BT.2020.";
        case SettingsHelpTopic::MjpegColor:
            return L"MJPEG color interpretation\n\n"
                   L"Auto uses decoder metadata first, then DirectShow metadata. If neither identifies "
                   L"the format, it uses JPEG Full range with BT.709 for HD or BT.601 for SD.\n\n"
                   L"Use a manual combination only when MJPEG colors still differ from another capture "
                   L"application. Full/Limited changes black and white levels; BT.709/BT.601 changes the "
                   L"YUV color matrix. This does not add a frame queue or increase latency.";
        }
    }
    switch (topic) {
    case SettingsHelpTopic::Drift:
         return L"소리 찢어짐 방지 · 보정 필요 확인\n\n"
                L"자동은 프로그램 내부 PCM 대기량을 관찰하다가 클록 불균형이 일정 시간 지속될 때만 "
                L"리샘플링을 켭니다. 한 번 켜지면 세션 중 반복해서 켰다 끄지 않아 소리 변화와 클릭을 "
                L"줄입니다. Tab OSD를 10~30분 확인하세요.\n\n"
               L"'안정 · 보정 불필요' 또는 '드문 오류 · 끔 유지 가능'이면 소리에 문제가 "
                L"없는 한 끔을 유지해도 됩니다. '반복 불균형 · 보정 권장'이 계속 보이면 자동을 "
                L"선택하세요. 시작 직후 오류만으로 판단하지 마세요.\n\n"
               L"리샘플러와 PCM 안전 대기량은 서로 독립입니다. '리샘플러 보정 한계 접근'은 "
               L"클록 차이, 'PCM 버퍼 부족 가능'은 순간 버퍼 여유 부족, '캡처 패킷 지연 감지'는 "
               L"입력 콜백 지연을 뜻합니다. 리샘플러가 정상인데 underrun이 나면 PCM 버퍼 "
               L"목표를 먼저 높이세요.\n\n"
                L"OSD의 불균형 ppm은 누적 underrun/overrun으로 계산한 참고값이며 실제 하드웨어 "
                L"클록을 직접 측정한 값은 아닙니다. 자동이 작동하면 작은 오디오 대기량을 추가하고 "
                L"PCM 샘플을 변경합니다. 끔은 원본 PCM을 유지하고, 켬은 항상 리샘플러를 사용합니다.";
    case SettingsHelpTopic::PcmQueue:
        return L"PCM 버퍼 목표 안내\n\n"
               L"캡처 오디오를 재생 전에 확보하는 프로그램 내부 대기량입니다.\n"
               L"10ms는 최저 지연, 15ms는 저지연 목표, 20ms는 안정 목표, 25ms는 권장 기본값, "
               L"30ms는 안정성 우선 설정입니다.\n\n"
               L"값을 높이면 순간적인 입력 지연을 흡수할 여유가 커지지만, 그만큼 오디오 지연이 "
               L"늘어납니다. WASAPI 출력 버퍼와 클록 드리프트 보정과는 독립적으로 조정됩니다.";
    case SettingsHelpTopic::Presentation:
        return llcv::presentation_ui::HelpText(false);
    case SettingsHelpTopic::VolumeBoost:
        return L"100% 이상 볼륨 증폭 안내\n\n"
               L"마우스 휠로 앱 음량을 최대 200%까지 올릴 수 있게 합니다. 100%는 원본 PCM 크기이고, "
               L"그 이상은 이 앱 안에서만 디지털 증폭을 적용합니다.\n\n"
               L"추가 오디오 버퍼나 프레임 큐를 만들지 않으므로 오디오 지연은 늘지 않습니다. 다만 원본 "
               L"소리가 이미 큰 경우에는 피크가 잘려 왜곡될 수 있으니, 실제로 음량이 부족할 때만 켜세요.";
    case SettingsHelpTopic::ForceHdr10:
        return L"HDR10 강제 출력 안내\n\n"
               L"전달 함수·색역 정보가 없는 P010은 기본적으로 BT.2020/PQ HDR10으로 해석하므로, "
               L"정보가 없다는 이유만으로 이 옵션을 켤 필요는 없습니다. P010은 HDR 전용이므로, 끈 상태에서 SDR로 보고된 입력은 차단합니다. SDR은 NV12/YUY2를 선택하세요.\n\n"
               L"입력이 실제 PQ/BT.2020인데 색 정보가 잘못되었거나 불완전한 경우에만 강제로 해석하세요. "
               L"색 범위·색차 배치 검증은 유지되며 SDR을 HDR로 변환하지는 않습니다. "
               L"색 정보가 없는 SDR 입력은 NV12/YUY2를 선택하세요. 표시 모니터의 Windows HDR을 켜야 합니다. "
               L"프레임 큐는 추가하지 않습니다.";
    case SettingsHelpTopic::HdrChroma:
        return L"HDR 색차 배치 안내\n\n"
               L"자동은 장치 메타데이터를 따르며 지원하지 않는 배치는 차단합니다. P010 HDR 장치의 "
               L"색차 배치 정보가 없거나 잘못된 경우에만 Top-left 또는 Left를 선택하고, 가는 색 경계와 "
               L"글자를 기준 화면과 비교하세요.\n\n"
               L"이 옵션은 배치 정보의 해석을 바꿉니다. 실제 Cb/Cr가 서로 어긋난 데이터를 복원하거나 "
               L"배치 값 6의 지원을 보장하지 않습니다. 밝기·채도·PQ 해석·색 범위 검증은 바꾸지 않으며 "
               L"프레임 큐나 처리 단계를 추가하지 않습니다. 전달 함수·색역 정보가 없는 P010은 기본적으로 PQ/BT.2020으로 해석합니다.";
    case SettingsHelpTopic::MjpegColor:
        return L"MJPEG 색상 해석 안내\n\n"
               L"자동은 디코더 메타데이터를 먼저 사용하고, 없으면 DirectShow 정보를 확인합니다. 양쪽 모두 "
               L"알려주지 않으면 JPEG Full range와 HD BT.709 또는 SD BT.601을 사용합니다.\n\n"
               L"자동 색상이 다른 캡처 프로그램과 계속 다를 때만 수동 조합을 선택하세요. Full/Limited는 "
               L"명암 범위를, BT.709/BT.601은 YUV 색상 행렬을 바꿉니다. 프레임 큐를 추가하지 않아 "
               L"표시 지연은 늘지 않습니다.";
    }
    return L"";
}

} // namespace llcv::settings_ui
