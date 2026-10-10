#pragma once

#include "audio/AudioEngine.h"
#include "capture/FrameMailbox.h"
#include "capture/V4l2Capture.h"
#include "capture/V4l2Devices.h"
#include "screenshot/Screenshot.h"
#include "settings/AppSettings.h"
#include "ui/HdrSupport.h"
#include "ui/ImGuiPlatform.h"
#include "ui/SettingsScreen.h"
#include "ui/Theme.h"
#include "ui/ViewerOverlay.h"
#include "video/HdrOutput.h"
#include "video/VideoRenderer.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace llcv::app {

struct LaunchOptions {
    std::optional<settings::DisplayBackend> backend;
    bool forceSettings = false;
    bool forceSdr = false;
};

class App {
public:
    App() = default;
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run(const LaunchOptions& options);

private:
    enum class Screen { Settings, Video, AudioOnly };

    bool InitializeSdl(settings::DisplayBackend backend);
    bool InitializeWindow();
    bool CreateGlWindow(int colorBits, bool gles, std::string& error);
    void DestroyGlWindow();
    void InitializeImGui();
    void Shutdown();
    void MainLoop();
    int WaitTimeoutMs() const;

    void HandleEvent(const SDL_Event& event);
    void HandleKey(const SDL_KeyboardEvent& key);
    void HandleWheel(const SDL_MouseWheelEvent& wheel);
    void HandleAudioDeviceRemoved(const SDL_AudioDeviceEvent& event);
    void Update();
    void UpdateStatistics(uint64_t nowMs);
    void UpdateCursor(uint64_t nowMs);

    void RenderFrame();
    void DrawSettings();
    void DrawVideoOverlays();
    void DrawAudioOnly();
    void DrawCommonOverlays();
    void DrawMenu();
    void ExecuteMenuCommand(ui::MenuCommand command);
    std::vector<ui::DiagnosticsLine> BuildDiagnostics() const;
    ui::AudioMeterState BuildAudioState() const;
    void ApplyAudioState(const ui::AudioMeterState& state);
    ui::SettingsContext BuildSettingsContext();

    void RefreshDevices();
    void RefreshAudioDevices();
    void RefreshDisplays();
    std::string ResolveCaptureAudio(const capture::CaptureDevice* device) const;
    bool StartSession(std::string& error);
    void StopSession();
    void EnterSettings();
    bool StartAudio(std::string& error);
    void ConfigureSessionWindow();
    void ApplyVideoWindowSize(bool useSavedSize);
    void ApplyWindowConstraints();
    void PositionOnPreferredDisplay();
    void SetFullscreen(bool fullscreen);
    void ApplySwapInterval();
    void RefreshPreferredColor();
    void UpdateHdrState();
    ui::HdrSupport EvaluateHdrSupport(const capture::CaptureDevice* device) const;
    ui::HdrSupport SessionHdrSupport() const;
    void ApplyLanguage();
    void ApplyStyle();
    void ApplyVolume();
    void UpdateBackgroundMute();
    void ChangeVolume(int delta);
    void RememberWindowGeometry();
    void RequestScreenshot();
    void PollScreenshots();
    void CopyPngToClipboard(std::vector<uint8_t> png);
    void ShowToast(std::string text, bool error = false);
    void OpenFolder(const std::filesystem::path& path);
    void SaveSettings();
    void NotifyFrame();
    bool IsWayland() const;

    settings::AppSettings settings_;
    std::filesystem::path settingsPath_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    bool sdlInitialized_ = false;
    bool imguiInitialized_ = false;
    bool rendererInitialized_ = false;
    uint32_t frameEvent_ = 0;
    std::string videoDriver_;
    std::string logDirectoryText_;
    std::string screenshotDirectoryText_;
    uint64_t lastRenderMs_ = 0;
    std::string kernelRelease_;
    int framebufferBits_ = 8;
    bool glEs_ = false;
    bool forceSdr_ = false;

    ui::ImGuiPlatform platform_;
    ui::FontSet fonts_;
    ui::SettingsScreen settingsScreen_;
    float scale_ = 1.0f;

    video::VideoRenderer renderer_;
    video::HdrOutput hdrOutput_;
    video::PreferredColor preferredColor_;
    bool hdrActive_ = false;
    capture::FrameMailbox mailbox_;
    capture::V4l2Capture capture_;
    audio::AudioEngine audio_;
    screenshot::Service screenshots_;

    std::vector<capture::CaptureDevice> captureDevices_;
    std::vector<audio::AudioDeviceEntry> recordingDevices_;
    std::vector<audio::AudioDeviceEntry> playbackDevices_;
    std::vector<ui::DisplayEntry> displays_;

    Screen screen_ = Screen::Settings;
    bool running_ = true;
    bool pendingStart_ = false;
    bool pendingStyle_ = false;
    bool audioDevicesChanged_ = false;
    int redrawFrames_ = 3;
    bool newVideoFrame_ = false;
    std::atomic<bool> framePending_{false};
    std::string errorMessage_;

    std::optional<capture::ResolvedMode> activeMode_;
    std::string activeDeviceName_;
    std::optional<capture::CaptureDevice> activeDevice_;
    std::string captureAudioName_;
    std::string audioError_;
    bool audioWanted_ = false;
    uint64_t audioRetryMs_ = 0;
    uint64_t sessionStartMs_ = 0;

    bool fullscreen_ = false;
    bool focused_ = true;
    bool diagnosticsVisible_ = false;
    bool audioOsdVisible_ = false;
    bool helpVisible_ = false;
    bool menuVisible_ = false;
    bool altTapPending_ = false;
    ui::MenuCommand pendingMenuCommand_ = ui::MenuCommand::None;
    bool cursorHidden_ = false;
    uint64_t lastMouseMoveMs_ = 0;
    float wheelAccumulator_ = 0.0f;
    uint64_t volumeHudUntilMs_ = 0;
    std::string toastText_;
    bool toastError_ = false;
    uint64_t toastUntilMs_ = 0;
    bool noSignalShown_ = false;
    capture::CaptureState lastCaptureState_ = capture::CaptureState::Stopped;

    uint64_t presentedFrames_ = 0;
    uint64_t statsWindowStartMs_ = 0;
    uint64_t statsCapturedBase_ = 0;
    uint64_t statsPresentedBase_ = 0;
    uint64_t statsProcessingNsBase_ = 0;
    uint64_t statsProcessingSamplesBase_ = 0;
    double latencySumMs_ = 0.0;
    uint64_t latencySamples_ = 0;
    double inputFps_ = 0.0;
    double presentFps_ = 0.0;
    double averageLatencyMs_ = -1.0;
    double averageProcessingMs_ = -1.0;
};

}
