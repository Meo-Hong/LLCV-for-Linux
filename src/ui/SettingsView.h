#pragma once

#include <windows.h>
#include <vector>
#include "ViewerShortcuts.h"

namespace llcv::settings { enum class VideoPixelFormat; }

namespace llcv::settings_ui {

enum class SettingsTab : int {
    Audio = 0,
    VideoWindow = 1,
    GuideDiagnostics = 2,
    Updates = 3,
    Window = 4,
};

inline constexpr int kSettingsNavigationCount = 5;
// Keep native strings, item data and input behavior; only row painting is custom.
inline constexpr DWORD kSettingsDropdownStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
    CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL;
SettingsTab SettingsTabFromNavigationIndex(int index);
int SettingsNavigationIndex(SettingsTab tab);

// UI-thread-only, non-owning control handles. The dialog owns the HWNDs and
// deletes uiFonts after its children have been destroyed. Hardware discovery,
// asynchronous workers and persistent settings deliberately do not belong here.
struct SettingsControls {
    bool english = false;
    // Supplied once by the controller; view refresh never probes hardware.
    bool vsrGpuUnavailable = false;
    bool vsrGpuUnknown = false;
    UINT layoutDpi = USER_DEFAULT_SCREEN_DPI;
    HWND brandLabel = nullptr;
    HWND pageTitle = nullptr;
    HWND pageSubtitle = nullptr;
    HWND appPreferencesSection = nullptr;
    HWND tabControl = nullptr;
    HWND guideVideoHint = nullptr;
    std::array<HWND, viewer_help::kShortcuts.size()> guideKeys{};
    std::array<HWND, viewer_help::kShortcuts.size()> guideDescriptions{};
    HWND guideShortcutsTitle = nullptr;
    HWND guideDiagnosticsTitle = nullptr;
    HWND guideDiagnosticsText = nullptr;
    HWND guideLogFolderButton = nullptr;
    HWND screenshotClipboardCheck = nullptr;
    HWND screenshotTitle = nullptr;
    HWND screenshotHelp = nullptr;
    HWND screenshotFolderButton = nullptr;
    HWND vsrCheck = nullptr;
    HWND vsrGuideButton = nullptr;
    HWND vsrStatus = nullptr;
    HWND vsrCaptureLabel = nullptr;
    HWND vsrCaptureCombo = nullptr;
    HWND updateTitle = nullptr;
    HWND updateText = nullptr;
    HWND updateNowButton = nullptr;
    HWND updateStatus = nullptr;
    HWND audioOutputSection = nullptr;
    HWND audioPlaybackSection = nullptr;
    HWND audioStabilitySection = nullptr;
    HWND videoCaptureSection = nullptr;
    HWND videoRefreshButton = nullptr;
    HWND videoDisplaySection = nullptr;
    HWND videoWindowSection = nullptr;
    HWND languageLabel = nullptr;
    HWND languageCombo = nullptr;
    HWND themeLabel = nullptr;
    HWND themeCombo = nullptr;
    HWND audioLabel = nullptr;
    HWND bufferLabel = nullptr;
    HWND audioOutputLabel = nullptr;
    HWND volumeHudLabel = nullptr;
    HWND volumeBoostCheck = nullptr;
    HWND volumeBoostHelp = nullptr;
    HWND driftLabel = nullptr;
    HWND driftHelp = nullptr;
    HWND pcmQueueLabel = nullptr;
    HWND pcmQueueHelp = nullptr;
    HWND presentationLabel = nullptr;
    HWND presentationHelp = nullptr;
    HWND fullscreenCursorLabel = nullptr;
    HWND fullscreenCursorHint = nullptr;
    HWND scalingLabel = nullptr;
    HWND videoLabel = nullptr;
    HWND captureDeviceLabel = nullptr;
    HWND captureAudioDeviceLabel = nullptr;
    HWND captureAudioStatus = nullptr;
    HWND pixelFormatLabel = nullptr;
    HWND frameRateLabel = nullptr;
    HWND videoCapabilityStatus = nullptr;
    HWND audioCombo = nullptr;
    HWND bufferCombo = nullptr;
    HWND audioOutputCombo = nullptr;
    HWND volumeHudCombo = nullptr;
    HWND muteBackgroundCheck = nullptr;
    HWND audioOnlyCheck = nullptr;
    HWND surround51Check = nullptr;
    HWND surround51Hint = nullptr;
    HWND forceHdr10Check = nullptr;
    HWND forceHdr10Help = nullptr;
    HWND hdrChromaLabel = nullptr;
    HWND hdrChromaCombo = nullptr;
    HWND hdrChromaHelp = nullptr;
    HWND mjpegColorLabel = nullptr;
    HWND mjpegColorCombo = nullptr;
    HWND mjpegColorHelp = nullptr;
    HWND driftCombo = nullptr;
    HWND pcmQueueCombo = nullptr;
    HWND audioStatus = nullptr;
    HWND exclusiveTestButton = nullptr;
    HWND presentationCombo = nullptr;
    HWND displayMonitorLabel = nullptr;
    HWND displayMonitorCombo = nullptr;
    HWND fullscreenCursorCombo = nullptr;
    HWND scalingCombo = nullptr;
    HWND videoCombo = nullptr;
    HWND captureDeviceCombo = nullptr;
    HWND captureAudioDeviceCombo = nullptr;
    HWND pixelFormatCombo = nullptr;
    HWND frameRateCombo = nullptr;
    HWND pixelCheck = nullptr;
    HWND relativeSizeCheck = nullptr;
    HWND relativeSizeWarning = nullptr;
    HWND borderlessCheck = nullptr;
    HWND roundedCornersCheck = nullptr;
    HWND windowSnapCheck = nullptr;
    HWND saveLogCheck = nullptr;
    HWND showConsoleCheck = nullptr;
    HWND skipStartupCheck = nullptr;
    HWND skipStartupHint = nullptr;
    HWND checkForUpdatesCheck = nullptr;
    HWND versionWatermark = nullptr;
    HWND startButton = nullptr;
    HWND cancelButton = nullptr;
    HWND tooltipWindow = nullptr;
    HWND activeTooltipTarget = nullptr;
    std::vector<HFONT> uiFonts;
    SettingsTab activeTab = SettingsTab::Audio;
};

enum class SettingsHelpTopic {
    Drift,
    PcmQueue,
    Presentation,
    VolumeBoost,
    ForceHdr10,
    HdrChroma,
    MjpegColor,
};

int SettingsPixels(int dips, UINT dpi);
inline constexpr int kSettingsClientWidthDip = 1000;
// Shared by native layout and its paint-only theme. Keep the shell compact
// enough for a small desktop while using one consistent content grid.
inline constexpr int kSettingsSidebarWidthDip = 156;
inline constexpr int kSettingsContentLeftDip = 184;
inline constexpr int kSettingsContentRightDip = 976;
inline constexpr int kSettingsFooterTopDip = 590;
inline constexpr int kSettingsComboHeightDip = 30;
inline constexpr int kSettingsBodyFontPoints = 10;
inline constexpr int kSettingsSecondaryFontPoints = 9;
inline constexpr int kSettingsTitleFontPoints = 20;
int SettingsClientHeightDip(const SettingsControls* state);
// Short, synchronous UI-only transaction. Never hold across modal dialogs or
// driver work. Nested and hidden-window scopes do not re-enable a hidden owner.
class SettingsVisualUpdate final {
public:
    explicit SettingsVisualUpdate(HWND owner);
    ~SettingsVisualUpdate();
    SettingsVisualUpdate(const SettingsVisualUpdate&) = delete;
    SettingsVisualUpdate& operator=(const SettingsVisualUpdate&) = delete;
private:
    HWND owner_ = nullptr;
};
SIZE SettingsDialogOuterSize(HWND hwnd, UINT dpi, const SettingsControls* state);
void PlaceSettingsControl(HWND control, int x, int y, int width, int height, UINT dpi);
void ApplySettingsFont(SettingsControls* state, HWND hwnd, UINT dpi);
void LayoutSettingsControls(SettingsControls* state, UINT dpi);
void RefreshSettingsPageHeader(SettingsControls* state);
void SetSettingsControlVisible(HWND control, bool visible, bool enabled = true);
void SetSettingsText(HWND control, const wchar_t* text);
void UpdateScalingControlVisibility(SettingsControls* state);
void UpdateWindowBehaviorVisibility(SettingsControls* state);
struct SettingsAvailability {
    bool start = true;
    bool formats = true;
    bool captureDevice = true;
    bool exclusiveProbe = true;
    bool audioBuffer = true;
};
void UpdateAdvancedControlVisibility(SettingsControls* state, bool exclusive,
                                     settings::VideoPixelFormat selectedFormat,
                                     SettingsAvailability availability = {});
void TrackSettingsTooltip(HWND target, HWND tooltip, bool active);
void AddSettingsTooltip(SettingsControls* state, HWND owner, HWND target, const wchar_t* text);
bool IsSettingsHelpControl(const SettingsControls* state, HWND target);
const wchar_t* SettingsHelpText(SettingsHelpTopic topic, bool english);

} // namespace llcv::settings_ui
