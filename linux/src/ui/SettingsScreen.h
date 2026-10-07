#pragma once

#include "audio/AudioEngine.h"
#include "capture/V4l2Devices.h"
#include "settings/AppSettings.h"
#include "ui/HdrSupport.h"
#include "ui/Theme.h"

#include <cstdint>
#include <string>
#include <vector>

namespace llcv::ui {

struct DisplayEntry {
    uint32_t id = 0;
    std::string name;
};

struct SettingsContext {
    const std::vector<capture::CaptureDevice>* captureDevices = nullptr;
    const std::vector<audio::AudioDeviceEntry>* recordingDevices = nullptr;
    const std::vector<audio::AudioDeviceEntry>* playbackDevices = nullptr;
    const std::vector<DisplayEntry>* displays = nullptr;
    std::string videoDriver;
    std::string audioDriver;
    std::string autoCaptureAudio;
    std::string errorMessage;
    std::string logDirectory;
    std::string screenshotDirectory;
    bool canStart = false;
    std::string startSummary;
    HdrSupport hdr;
    int framebufferBits = 8;
    FontSet fonts;
    float scale = 1.0f;
};

enum class SettingsAction {
    None,
    Start,
    Quit,
    RefreshDevices,
    ThemeChanged,
    LanguageChanged,
    LogToggled,
    OpenLogFolder,
    OpenScreenshotFolder,
};

class SettingsScreen {
public:
    SettingsAction Draw(settings::AppSettings& settings, const SettingsContext& context);

private:
    enum class Page { Video, Audio, Window, Guide, App };

    void DrawSidebar(const SettingsContext& context, float height);
    void DrawFooter(const SettingsContext& context);
    void DrawVideo(settings::AppSettings& settings, const SettingsContext& context);
    void DrawHdr(settings::AppSettings& settings, const SettingsContext& context);
    void DrawAudio(settings::AppSettings& settings, const SettingsContext& context);
    void DrawWindow(settings::AppSettings& settings, const SettingsContext& context);
    void DrawGuide(settings::AppSettings& settings, const SettingsContext& context);
    void DrawApp(settings::AppSettings& settings, const SettingsContext& context);

    Page page_ = Page::Video;
    SettingsAction action_ = SettingsAction::None;
};

}
