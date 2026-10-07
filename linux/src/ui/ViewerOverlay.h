#pragma once

#include "settings/AppSettings.h"
#include "ui/HdrSupport.h"
#include "ui/Theme.h"

#include <cstdint>
#include <string>
#include <vector>

namespace llcv::ui {

struct DiagnosticsLine {
    std::string label;
    std::string value;
};

struct AudioMeterState {
    int master = 100;
    int left = 100;
    int right = 100;
    bool boost = false;
    bool available = false;
    bool muted = false;
    int peakLeft = 0;
    int peakRight = 0;
    uint64_t clipEvents = 0;
    bool clipActive = false;
    std::string captureName;
    std::string outputName;
    std::string status;
};

void DrawDiagnostics(const std::vector<DiagnosticsLine>& lines, const FontSet& fonts, float scale);
void DrawVolumeHud(int volume, bool muted, settings::VolumeHudPosition position, float alpha, float scale);
void DrawToast(const std::string& text, bool error, float alpha, float scale);
void DrawCenterNotice(const std::string& title, const std::string& detail, const FontSet& fonts, float scale);
bool DrawAudioOsd(AudioMeterState& state, const FontSet& fonts, float scale);
bool DrawAudioOnlyView(AudioMeterState& state, const FontSet& fonts, float scale);
void DrawHelp(bool* open, bool audioOnly, const HdrSupport& hdr, const FontSet& fonts, float scale);

enum class MenuCommand {
    None,
    Fullscreen,
    RestoreSize,
    Borderless,
    ScalingSmooth,
    ScalingSharp,
    PresentImmediate,
    PresentVSync,
    Diagnostics,
    AudioOsd,
    VolumeUp,
    VolumeDown,
    BackgroundMute,
    Screenshot,
    OpenScreenshots,
    OpenLogs,
    Settings,
    Help,
    Quit,
};

struct MenuState {
    bool audioOnly = false;
    bool fullscreen = false;
    bool borderless = false;
    bool sharp = false;
    bool vsync = false;
    bool diagnostics = false;
    bool audioOsd = false;
    bool backgroundMute = false;
    int volume = 100;
};

MenuCommand DrawMenuBar(const MenuState& state, float scale);

}
