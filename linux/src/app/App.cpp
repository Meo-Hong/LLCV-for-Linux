#include "app/App.h"

#include "app/AppInfo.h"
#include "app/ModeSelection.h"
#include "capture/UvcDescriptors.h"
#include "diagnostics/Logger.h"
#include "input/Shortcuts.h"
#include "platform/Clock.h"
#include "platform/Paths.h"
#include "platform/PngImage.h"
#include "platform/Strings.h"
#include "recording/RecordingStorage.h"
#include "settings/SettingsStore.h"
#include "ui/Text.h"
#include "video/GlApi.h"

#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <system_error>

namespace llcv::app {
namespace {

using diagnostics::Log;
using platform::Format;
using ui::T;

constexpr int kSettingsWidth = 1000;
constexpr int kSettingsHeight = 680;
constexpr uint64_t kCursorHideMs = 2000;
constexpr uint64_t kVolumeHudMs = 1500;
constexpr uint64_t kFadeMs = 300;
constexpr uint64_t kNoSignalMs = 1500;

struct ClipboardPng {
    std::vector<uint8_t> data;
};

const void* SDLCALL ProvideClipboard(void* userdata, const char* mimeType, size_t* size) {
    auto* png = static_cast<ClipboardPng*>(userdata);
    if (!png || !mimeType || std::strcmp(mimeType, "image/png") != 0) {
        *size = 0;
        return nullptr;
    }
    *size = png->data.size();
    return png->data.data();
}

void SDLCALL ReleaseClipboard(void* userdata) {
    delete static_cast<ClipboardPng*>(userdata);
}

const char* BackendHint(settings::DisplayBackend backend) {
    switch (backend) {
    case settings::DisplayBackend::Wayland: return "wayland";
    case settings::DisplayBackend::X11: return "x11";
    case settings::DisplayBackend::Auto: break;
    }
    return nullptr;
}

unsigned long long U(uint64_t value) {
    return static_cast<unsigned long long>(value);
}

double FramesToMs(uint64_t frames) {
    return static_cast<double>(frames) * 1000.0 / audio::AudioEngine::kSampleRate;
}

std::string Ms(double value) {
    return value < 0.0 ? std::string("-") : Format("%.2f ms", value);
}

float FadeAlpha(uint64_t now, uint64_t until) {
    if (now < until) return 1.0f;
    if (now >= until + kFadeMs) return 0.0f;
    return 1.0f - static_cast<float>(now - until) / static_cast<float>(kFadeMs);
}

std::string StartErrorText(recording::StartError error) {
    switch (error) {
    case recording::StartError::Busy: return T("The previous recording is still being finalized.");
    case recording::StartError::Unavailable:
        return T("This build has no recording support. Rebuild LLCV with the FFmpeg development files installed.");
    case recording::StartError::UnsupportedFormat: return T("This input format cannot be recorded.");
    case recording::StartError::HdrInput:
        return T("HDR input cannot be recorded. Recording supports SDR video only.");
    case recording::StartError::StorageUnavailable: return T("The recording folder is unavailable.");
    case recording::StartError::DiskFull: return T("Not enough free space in the recording folder.");
    case recording::StartError::None: break;
    }
    return {};
}

std::string FailureText(recording::Failure failure) {
    switch (failure) {
    case recording::Failure::DiskFull: return T("Recording stopped: not enough disk space.");
    case recording::Failure::WriteFailed: return T("Recording stopped: the file could not be written.");
    case recording::Failure::Overload:
        return T("Recording stopped: encoding or writing could not keep up. Lower the recording resolution or frame "
                 "limit.");
    case recording::Failure::EncoderFailed: return T("Recording stopped: the encoder reported an error.");
    case recording::Failure::StorageUnavailable: return T("The recording folder is unavailable.");
    case recording::Failure::FinalizeFailed: return T("The recording file could not be finalized.");
    case recording::Failure::None: break;
    }
    return T("Recording could not be completed.");
}

const char* CodecName(recording::Codec codec) {
    switch (codec) {
    case recording::Codec::Hevc: return "HEVC";
    case recording::Codec::Av1: return "AV1";
    case recording::Codec::H264: break;
    }
    return "H.264";
}

std::string Elapsed(uint64_t milliseconds) {
    const unsigned long long seconds = milliseconds / 1000;
    return Format("%02llu:%02llu:%02llu", seconds / 3600, seconds / 60 % 60, seconds % 60);
}

}

App::~App() {
    Shutdown();
}

int App::Run(const LaunchOptions& options) {
    forceSdr_ = options.forceSdr;
    settingsPath_ = platform::SettingsFilePath();
    settings_ = settings::LoadSettings(settingsPath_);
    diagnostics::Logger::Instance().SetFileLogging(settings_.saveLog);
    Log("LLCV %s starting, settings %s", kVersion, settingsPath_.c_str());

    if (!InitializeSdl(options.backend.value_or(settings_.displayBackend))) return 1;
    if (!SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH)) {
        Log("[thread] render thread priority unchanged: %s", SDL_GetError());
    }
    ApplyLanguage();
    if (!InitializeWindow()) return 1;
    InitializeImGui();
    capture_.SetRawFrameSink(&recorder_);
    audio_.SetCaptureTap(&recorder_);
    screenshotDirectoryText_ = platform::ScreenshotDirectory().string();
    kernelRelease_ = capture::KernelRelease();
    logDirectoryText_ = platform::LogDirectory().string();
    RefreshDevices();

    bool started = false;
    if (settings_.skipStartupSettings && !options.forceSettings) {
        std::string error;
        started = StartSession(error);
        if (!started) {
            errorMessage_ = error;
            Log("[session] start failed: %s", error.c_str());
        }
    }
    if (!started) EnterSettings();

    MainLoop();
    StopSession();
    SaveSettings();
    Shutdown();
    return 0;
}

bool App::InitializeSdl(settings::DisplayBackend backend) {
    SDL_SetAppMetadata(kAppName, kVersion, kAppId);
    SDL_SetHint(SDL_HINT_APP_ID, kAppId);
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    const char* hint = BackendHint(backend);
    if (hint) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, hint);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        Log("[sdl] video init failed (%s): %s", hint ? hint : "auto", SDL_GetError());
        if (!hint) return false;
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            Log("[sdl] video init failed: %s", SDL_GetError());
            return false;
        }
    }
    sdlInitialized_ = true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) Log("[sdl] audio init failed: %s", SDL_GetError());
    const char* driver = SDL_GetCurrentVideoDriver();
    videoDriver_ = driver ? driver : "";
    const char* audioDriver = SDL_GetCurrentAudioDriver();
    const int version = SDL_GetVersion();
    Log("[sdl] SDL %d.%d.%d, video %s, audio %s", SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
        SDL_VERSIONNUM_MICRO(version), videoDriver_.c_str(), audioDriver ? audioDriver : "none");
    frameEvent_ = SDL_RegisterEvents(1);
    return frameEvent_ != 0;
}

bool App::CreateGlWindow(int colorBits, bool gles, std::string& error) {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gles ? 0 : 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, gles ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, colorBits);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, colorBits);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, colorBits);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);

    const SDL_WindowFlags flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    window_ = SDL_CreateWindow(kAppName, kSettingsWidth, kSettingsHeight, flags);
    if (!window_) {
        error = std::string("window: ") + SDL_GetError();
        return false;
    }
    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        error = std::string(gles ? "OpenGL ES 3.0 context: " : "OpenGL 3.3 context: ") + SDL_GetError();
        return false;
    }
    SDL_GL_MakeCurrent(window_, glContext_);
    int red = 0;
    SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &red);
    framebufferBits_ = red > 0 ? red : 8;
    if (framebufferBits_ < colorBits) {
        error = Format("requested %d-bit color, got %d-bit", colorBits, framebufferBits_);
        return false;
    }
    return true;
}

void App::DestroyGlWindow() {
    if (glContext_) {
        SDL_GL_DestroyContext(glContext_);
        glContext_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

bool App::InitializeWindow() {
    const bool deepColor = !forceSdr_ && video::HdrOutput::Compiled() && IsWayland() &&
                           settings_.hdrOutput != settings::HdrOutputMode::ToneMap;
    std::string error;
    bool created = false;
    for (const bool gles : {false, true}) {
        if (deepColor) {
            created = CreateGlWindow(10, gles, error);
            if (!created) {
                Log("[gl] 10-bit framebuffer unavailable (%s); using 8-bit", error.c_str());
                DestroyGlWindow();
                error.clear();
            }
        }
        if (!created) created = CreateGlWindow(8, gles, error);
        if (created) {
            glEs_ = gles;
            error.clear();
            break;
        }
        Log("[gl] %s", error.c_str());
        DestroyGlWindow();
    }
    if (created) {
        std::string missing;
        if (!video::LoadGlApi(missing)) error = "OpenGL function missing: " + missing;
    }
    if (error.empty() && !renderer_.Initialize(error, glEs_)) error = "renderer: " + error;
    if (!error.empty()) {
        Log("[gl] %s", error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kAppName, error.c_str(), window_);
        return false;
    }
    rendererInitialized_ = true;
    Log("[gl] %s / %s / %s, %d-bit framebuffer", reinterpret_cast<const char*>(video::gl.GetString(GL_VENDOR)),
        reinterpret_cast<const char*>(video::gl.GetString(GL_RENDERER)),
        reinterpret_cast<const char*>(video::gl.GetString(GL_VERSION)), framebufferBits_);
    if (IsWayland() && !forceSdr_) hdrOutput_.Initialize(window_);

    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    const auto icon = platform::FindAppIcon();
    if (!icon.empty() && platform::LoadPngRgba(icon, pixels, width, height)) {
        if (SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * 4)) {
            SDL_SetWindowIcon(window_, surface);
            SDL_DestroySurface(surface);
        }
    }
    return true;
}

void App::InitializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    platform_.Initialize(window_);
    ImGui_ImplOpenGL3_Init(glEs_ ? "#version 300 es" : "#version 330 core");
    fonts_ = ui::LoadFonts();
    imguiInitialized_ = true;
    ApplyStyle();
}

void App::Shutdown() {
    recorder_.Stop();
    if (!recorder_.WaitIdle(15000)) Log("[recording] still finalizing at shutdown");
    capture_.Stop();
    audio_.Stop();
    capture_.SetRawFrameSink(nullptr);
    audio_.SetCaptureTap(nullptr);
    sleepInhibitor_.Release();
    hdrOutput_.Shutdown();
    if (imguiInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        platform_.Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
    if (rendererInitialized_) {
        renderer_.Shutdown();
        rendererInitialized_ = false;
    }
    DestroyGlWindow();
    if (sdlInitialized_) {
        SDL_Quit();
        sdlInitialized_ = false;
    }
}

void App::MainLoop() {
    while (running_) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, WaitTimeoutMs())) {
            HandleEvent(event);
            while (SDL_PollEvent(&event)) HandleEvent(event);
        }
        Update();
        if (!running_) break;
        if (newVideoFrame_ || redrawFrames_ > 0) {
            RenderFrame();
            if (redrawFrames_ > 0) --redrawFrames_;
        }
        if (pendingStyle_) {
            pendingStyle_ = false;
            ApplyStyle();
            redrawFrames_ = std::max(redrawFrames_, 3);
        }
        if (pendingMenuCommand_ != ui::MenuCommand::None) {
            const ui::MenuCommand command = pendingMenuCommand_;
            pendingMenuCommand_ = ui::MenuCommand::None;
            ExecuteMenuCommand(command);
            redrawFrames_ = std::max(redrawFrames_, 2);
        }
        if (pendingExit_ != ExitRequest::None) {
            const ExitRequest request = pendingExit_;
            pendingExit_ = ExitRequest::None;
            PerformExit(request);
            redrawFrames_ = std::max(redrawFrames_, 3);
        }
        if (pendingStart_) {
            pendingStart_ = false;
            std::string error;
            if (!StartSession(error)) {
                errorMessage_ = error;
                Log("[session] start failed: %s", error.c_str());
            }
            redrawFrames_ = std::max(redrawFrames_, 3);
        }
    }
}

int App::WaitTimeoutMs() const {
    if (newVideoFrame_ || redrawFrames_ > 0) return 0;
    const uint64_t now = platform::MonotonicMs();
    if (now < volumeHudUntilMs_ + kFadeMs || now < toastUntilMs_ + kFadeMs) return 16;
    if (screen_ == Screen::Settings) return 500;
    if (diagnosticsVisible_ || audioOsdVisible_ || screen_ == Screen::AudioOnly) return 100;
    return 250;
}

void App::HandleEvent(const SDL_Event& event) {
    if (event.type == frameEvent_) {
        framePending_.store(false, std::memory_order_release);
        newVideoFrame_ = true;
        return;
    }
    if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && CaptureShortcutKey(event.key)) {
        redrawFrames_ = std::max(redrawFrames_, 2);
        return;
    }
    if (imguiInitialized_) platform_.ProcessEvent(event);
    redrawFrames_ = std::max(redrawFrames_, 2);

    switch (event.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        RequestQuit();
        break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        pendingStyle_ = true;
        if (screen_ == Screen::Video && settings_.pixelPerfect && !fullscreen_) ApplyVideoWindowSize(true);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        focused_ = true;
        UpdateBackgroundMute();
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        focused_ = false;
        altTapPending_ = false;
        settingsScreen_.CancelShortcutCapture();
        UpdateBackgroundMute();
        break;
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
        RememberWindowGeometry();
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        fullscreen_ = true;
        break;
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        fullscreen_ = false;
        ApplyWindowConstraints();
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_LALT || event.key.key == SDLK_RALT) {
            if (!event.key.repeat) altTapPending_ = true;
        } else {
            altTapPending_ = false;
        }
        HandleKey(event.key);
        break;
    case SDL_EVENT_KEY_UP:
        if ((event.key.key == SDLK_LALT || event.key.key == SDLK_RALT) && altTapPending_ &&
            screen_ != Screen::Settings) {
            menuVisible_ = !menuVisible_;
            lastMouseMoveMs_ = platform::MonotonicMs();
        }
        altTapPending_ = false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        lastMouseMoveMs_ = platform::MonotonicMs();
        if (cursorHidden_) {
            SDL_ShowCursor();
            cursorHidden_ = false;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        HandleWheel(event.wheel);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        altTapPending_ = false;
        if (menuVisible_ && !ImGui::GetIO().WantCaptureMouse) {
            menuVisible_ = false;
            break;
        }
        if (event.button.button == SDL_BUTTON_LEFT && event.button.clicks == 2 && screen_ == Screen::Video &&
            !ImGui::GetIO().WantCaptureMouse) {
            SetFullscreen(!fullscreen_);
        }
        break;
    case SDL_EVENT_AUDIO_DEVICE_ADDED:
        audioDevicesChanged_ = true;
        break;
    case SDL_EVENT_AUDIO_DEVICE_REMOVED:
        audioDevicesChanged_ = true;
        HandleAudioDeviceRemoved(event.adevice);
        break;
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
        RefreshDisplays();
        break;
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        RefreshPreferredColor();
        UpdateHdrState();
        FollowDisplayChange();
        break;
    default:
        break;
    }
}

bool App::CaptureShortcutKey(const SDL_KeyboardEvent& key) {
    if (shortcutKeyToRelease_ != SDL_SCANCODE_UNKNOWN && key.scancode == shortcutKeyToRelease_) {
        if (!key.down || key.repeat) {
            if (!key.down) shortcutKeyToRelease_ = SDL_SCANCODE_UNKNOWN;
            return true;
        }
        shortcutKeyToRelease_ = SDL_SCANCODE_UNKNOWN;
    }
    if (screen_ != Screen::Settings || !settingsScreen_.CapturingShortcut()) return false;
    if (!key.down || key.repeat || input::IsModifierKey(key.scancode)) return true;
    if (settingsScreen_.CaptureShortcut(settings_, input::FromKeyEvent(key))) shortcutKeyToRelease_ = key.scancode;
    return true;
}

void App::HandleKey(const SDL_KeyboardEvent& key) {
    if (key.repeat || screen_ == Screen::Settings) return;
    const auto action = input::FindAction(settings_.shortcuts, input::FromKeyEvent(key));
    if (confirmExit_ != ExitRequest::None) {
        if (action == input::Action::Exit) confirmExit_ = ExitRequest::None;
        return;
    }
    if ((key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) && (key.mod & SDL_KMOD_ALT)) {
        SetFullscreen(!fullscreen_);
        return;
    }
    if (!action) return;
    switch (*action) {
    case input::Action::Help:
        helpVisible_ = !helpVisible_;
        break;
    case input::Action::Settings:
        RequestSettings();
        break;
    case input::Action::AudioOsd:
        if (screen_ == Screen::Video) audioOsdVisible_ = !audioOsdVisible_;
        break;
    case input::Action::PixelPerfect:
        RestoreOneToOne();
        break;
    case input::Action::Fullscreen:
        SetFullscreen(!fullscreen_);
        break;
    case input::Action::Screenshot:
        if (screen_ == Screen::Video) RequestScreenshot();
        break;
    case input::Action::Diagnostics:
        diagnosticsVisible_ = !diagnosticsVisible_;
        if (diagnosticsVisible_) audio_.ResetMinimum();
        break;
    case input::Action::Exit:
        if (menuVisible_) {
            menuVisible_ = false;
        } else if (helpVisible_) {
            helpVisible_ = false;
        } else if (fullscreen_) {
            SetFullscreen(false);
        } else {
            RequestQuit();
        }
        break;
    case input::Action::Recording:
        ToggleRecording();
        break;
    case input::Action::Mute:
        ToggleMute();
        break;
    default:
        break;
    }
}

void App::HandleWheel(const SDL_MouseWheelEvent& wheel) {
    if (screen_ == Screen::Settings) return;
    const bool overUi = screen_ == Screen::AudioOnly ? ImGui::IsAnyItemHovered() : ImGui::GetIO().WantCaptureMouse;
    if (overUi) return;
    wheelAccumulator_ += wheel.y;
    while (wheelAccumulator_ >= 1.0f) {
        ChangeVolume(5);
        wheelAccumulator_ -= 1.0f;
    }
    while (wheelAccumulator_ <= -1.0f) {
        ChangeVolume(-5);
        wheelAccumulator_ += 1.0f;
    }
}

void App::HandleAudioDeviceRemoved(const SDL_AudioDeviceEvent& event) {
    if (!audio_.Running() || !audio_.OwnsDevice(event.which)) return;
    Log("[audio] device %u removed during playback", static_cast<unsigned>(event.which));
    audio_.Stop();
    audioError_ = T("Audio device disconnected · it is restored automatically when it returns");
    ShowToast(audioError_, true);
    audioRetryMs_ = platform::MonotonicMs() + 1000;
}

void App::Update() {
    const uint64_t now = platform::MonotonicMs();
    PollScreenshots();
    PollRecording();
    {
        std::lock_guard<std::mutex> lock(folderMutex_);
        if (chosenFolder_) {
            settings_.recording.folder = *chosenFolder_;
            chosenFolder_.reset();
            redrawFrames_ = std::max(redrawFrames_, 1);
        }
        if (!folderError_.empty()) {
            errorMessage_ = Format(T("The folder dialog is unavailable: %s"), folderError_.c_str());
            folderError_.clear();
            redrawFrames_ = std::max(redrawFrames_, 1);
        }
    }

    if (audioDevicesChanged_) {
        audioDevicesChanged_ = false;
        RefreshAudioDevices();
        if (screen_ != Screen::Settings && audioWanted_ && !audio_.Running()) audioRetryMs_ = now;
    }
    if (screen_ != Screen::Settings && audioWanted_ && !audio_.Running() && audioRetryMs_ != 0 &&
        now >= audioRetryMs_) {
        std::string error;
        if (StartAudio(error)) {
            audioError_.clear();
            audioRetryMs_ = 0;
            ShowToast(T("Audio restored."));
        } else {
            audioError_ = error;
            audioRetryMs_ = now + 2000;
        }
        redrawFrames_ = std::max(redrawFrames_, 1);
    }

    if (screen_ == Screen::Video) {
        const auto state = capture_.State();
        if (state != lastCaptureState_) {
            lastCaptureState_ = state;
            UpdateSleepPrevention();
            redrawFrames_ = std::max(redrawFrames_, 1);
        }
        const uint64_t lastFrameMs = capture_.Counters().lastFrameNs.load(std::memory_order_acquire) / 1000000ull;
        const bool noSignal = lastFrameMs == 0 ? now - sessionStartMs_ > kNoSignalMs : now - lastFrameMs > kNoSignalMs;
        if (noSignal != noSignalShown_) {
            noSignalShown_ = noSignal;
            redrawFrames_ = std::max(redrawFrames_, 1);
        }
    }

    if (hdrOutput_.TakePreferredChanged()) {
        RefreshPreferredColor();
        UpdateHdrState();
        redrawFrames_ = std::max(redrawFrames_, 1);
    }

    UpdateStatistics(now);
    UpdateCursor(now);

    const bool animating = now < volumeHudUntilMs_ + kFadeMs || now < toastUntilMs_ + kFadeMs;
    const bool liveOverlay =
        screen_ != Screen::Settings && (diagnosticsVisible_ || audioOsdVisible_ || screen_ == Screen::AudioOnly);
    const bool recordingShown = screen_ == Screen::Video && recorder_.Busy();
    if (animating || (liveOverlay && now - lastRenderMs_ >= 100) || (recordingShown && now - lastRenderMs_ >= 500)) {
        redrawFrames_ = std::max(redrawFrames_, 1);
    }
}

void App::UpdateStatistics(uint64_t now) {
    if (statsWindowStartMs_ == 0) {
        statsWindowStartMs_ = now;
        return;
    }
    const uint64_t elapsed = now - statsWindowStartMs_;
    if (elapsed < 500) return;
    const auto& counters = capture_.Counters();
    const uint64_t captured = counters.frames.load(std::memory_order_relaxed);
    const uint64_t processingNs = counters.processingNsTotal.load(std::memory_order_relaxed);
    const uint64_t processingSamples = counters.processingSamples.load(std::memory_order_relaxed);
    if (captured < statsCapturedBase_) statsCapturedBase_ = 0;
    if (processingSamples < statsProcessingSamplesBase_) {
        statsProcessingSamplesBase_ = 0;
        statsProcessingNsBase_ = 0;
    }
    const double seconds = static_cast<double>(elapsed) / 1000.0;
    inputFps_ = static_cast<double>(captured - statsCapturedBase_) / seconds;
    presentFps_ = static_cast<double>(presentedFrames_ - statsPresentedBase_) / seconds;
    averageLatencyMs_ = latencySamples_ ? latencySumMs_ / static_cast<double>(latencySamples_) : -1.0;
    const uint64_t samples = processingSamples - statsProcessingSamplesBase_;
    averageProcessingMs_ =
        samples ? static_cast<double>(processingNs - statsProcessingNsBase_) / 1.0e6 / static_cast<double>(samples)
                : -1.0;
    statsCapturedBase_ = captured;
    statsPresentedBase_ = presentedFrames_;
    statsProcessingNsBase_ = processingNs;
    statsProcessingSamplesBase_ = processingSamples;
    latencySumMs_ = 0.0;
    latencySamples_ = 0;
    statsWindowStartMs_ = now;
}

void App::UpdateSleepPrevention() {
    const bool playing = screen_ == Screen::AudioOnly ||
                         (screen_ == Screen::Video && capture_.State() == capture::CaptureState::Streaming);
    if (!settings_.preventSleep || !playing) {
        sleepInhibitor_.Release();
        return;
    }
    sleepInhibitor_.Acquire(screen_ == Screen::Video,
                            T("LLCV capture playback is active."));
}

void App::UpdateCursor(uint64_t now) {
    const bool autoHide = screen_ == Screen::Video && fullscreen_ &&
                          settings_.fullscreenCursor == settings::FullscreenCursor::AutoHide && !helpVisible_ &&
                          !audioOsdVisible_ && !menuVisible_ && confirmExit_ == ExitRequest::None;
    if (autoHide && !cursorHidden_ && now - lastMouseMoveMs_ > kCursorHideMs) {
        SDL_HideCursor();
        cursorHidden_ = true;
    } else if (!autoHide && cursorHidden_) {
        SDL_ShowCursor();
        cursorHidden_ = false;
    }
}

void App::RenderFrame() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    ApplyPlacement();

    uint64_t frameCaptureNs = 0;
    if (newVideoFrame_) {
        newVideoFrame_ = false;
        if (screen_ == Screen::Video) {
            if (const capture::VideoFrame* frame = mailbox_.AcquireLatest()) {
                renderer_.Upload(*frame);
                frameCaptureNs = frame->captureNs;
            }
        }
    }

    ImGui_ImplOpenGL3_NewFrame();
    platform_.NewFrame();
    ImGui::NewFrame();
    switch (screen_) {
    case Screen::Settings: DrawSettings(); break;
    case Screen::Video: DrawVideoOverlays(); break;
    case Screen::AudioOnly: DrawAudioOnly(); break;
    }
    ImGui::Render();

    const bool sharp = settings_.scalingMode == settings::ScalingMode::Sharp;
    if (screen_ == Screen::Video && hdrActive_) {
        renderer_.BeginUiLayer(width, height);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        renderer_.EndUiLayer();
        renderer_.ComposeHdr(width, height, sharp);
    } else {
        if (screen_ == Screen::Video) {
            renderer_.Draw(width, height, sharp);
        } else {
            const ImVec4 background = ui::ToVec4(ui::CurrentPalette().background);
            renderer_.Clear(width, height, background.x, background.y, background.z);
        }
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    SDL_GL_SwapWindow(window_);
    lastRenderMs_ = platform::MonotonicMs();

    if (frameCaptureNs) {
        ++presentedFrames_;
        const uint64_t now = platform::MonotonicNs();
        if (now > frameCaptureNs) {
            latencySumMs_ += static_cast<double>(now - frameCaptureNs) / 1.0e6;
            ++latencySamples_;
        }
    }
}

void App::DrawSettings() {
    const ui::SettingsContext context = BuildSettingsContext();
    const ui::SettingsAction action = settingsScreen_.Draw(settings_, context);
    if (settingsScreen_.TakeRecordingPageOpened()) encoderProbe_.Start();
    switch (action) {
    case ui::SettingsAction::Start:
        settingsBeforeEdit_.reset();
        pendingStart_ = true;
        break;
    case ui::SettingsAction::Quit:
        running_ = false;
        break;
    case ui::SettingsAction::RefreshDevices:
        RefreshDevices();
        break;
    case ui::SettingsAction::ThemeChanged:
        pendingStyle_ = true;
        break;
    case ui::SettingsAction::LanguageChanged:
        ApplyLanguage();
        break;
    case ui::SettingsAction::LogToggled:
        diagnostics::Logger::Instance().SetFileLogging(settings_.saveLog);
        break;
    case ui::SettingsAction::OpenLogFolder:
        OpenFolder(platform::LogDirectory());
        break;
    case ui::SettingsAction::OpenScreenshotFolder:
        OpenFolder(platform::ScreenshotDirectory());
        break;
    case ui::SettingsAction::OpenRecordingFolder:
        OpenFolder(recording::RecordingDirectory(settings_.recording));
        break;
    case ui::SettingsAction::ChooseRecordingFolder:
        ChooseRecordingFolder();
        break;
    case ui::SettingsAction::OpenLastRecording:
        OpenLastRecording();
        break;
    case ui::SettingsAction::Cancel:
        if (settingsBeforeEdit_) {
            const bool themeChanged = settings_.lightTheme != settingsBeforeEdit_->lightTheme;
            settings_ = *settingsBeforeEdit_;
            settingsBeforeEdit_.reset();
            ApplyLanguage();
            diagnostics::Logger::Instance().SetFileLogging(settings_.saveLog);
            if (themeChanged) pendingStyle_ = true;
            pendingStart_ = true;
        }
        break;
    case ui::SettingsAction::None:
        break;
    }
}

void App::DrawVideoOverlays() {
    if (capture_.State() == capture::CaptureState::Reconnecting) {
        ui::DrawCenterNotice(T("Capture device disconnected"),
                             T("Playback resumes automatically when the device reconnects."),
                             fonts_, scale_);
    } else if (noSignalShown_) {
        ui::DrawCenterNotice(T("Waiting for signal"),
                             T("Check the HDMI source and the capture device. HDR output must be off."),
                             fonts_, scale_);
    }
    if (audioOsdVisible_) {
        ui::AudioMeterState state = BuildAudioState();
        if (ui::DrawAudioOsd(state, settings_.shortcuts, fonts_, scale_)) ApplyAudioState(state);
    } else if (playbackMuted_) {
        ui::DrawMuteIndicator(scale_);
    }
    if (recorder_.Busy()) ui::DrawRecordingIndicator(recorder_.GetStatus(), scale_);
    DrawCommonOverlays();
}

void App::DrawAudioOnly() {
    ui::AudioMeterState state = BuildAudioState();
    const bool changed = ui::DrawAudioOnlyView(state, settings_.shortcuts, fonts_, scale_);
    audioOnlyControls_ = {state.controlsMin.x, state.controlsMin.y, state.controlsMax.x, state.controlsMax.y};
    if (changed) ApplyAudioState(state);
    DrawCommonOverlays();
}

void App::DrawCommonOverlays() {
    const uint64_t now = platform::MonotonicMs();
    if (diagnosticsVisible_) ui::DrawDiagnostics(BuildDiagnostics(), settings_.shortcuts, fonts_, scale_);
    const float hudAlpha = FadeAlpha(now, volumeHudUntilMs_);
    if (hudAlpha > 0.0f) {
        ui::DrawVolumeHud(settings_.volumePercent, playbackMuted_ || (settings_.muteWhenBackground && !focused_),
                          settings_.volumeHudPosition, hudAlpha, scale_);
    }
    const float toastAlpha = FadeAlpha(now, toastUntilMs_);
    if (toastAlpha > 0.0f) ui::DrawToast(toastText_, toastError_, toastAlpha, scale_);
    if (helpVisible_) {
        ui::DrawHelp(&helpVisible_, screen_ == Screen::AudioOnly, SessionHdrSupport(), settings_.shortcuts, fonts_,
                     scale_);
    }
    if (menuVisible_) DrawMenu();
    if (confirmExit_ != ExitRequest::None) DrawRecordingConfirm();
}

std::vector<ui::DiagnosticsLine> App::BuildDiagnostics() const {
    std::vector<ui::DiagnosticsLine> lines;
    lines.push_back({T("Path"),
                     Format("V4L2 · %s · %s%s", glEs_ ? "OpenGL ES" : "OpenGL", videoDriver_.c_str(),
                            fullscreen_ ? T(" · fullscreen") : "")});
    if (screen_ == Screen::Video && activeMode_) {
        const auto negotiated = capture_.Negotiated();
        const auto& color = negotiated.color;
        lines.push_back({T("Device"), activeDeviceName_});
        lines.push_back({T("Input"),
                         Format("%d × %d @ %s fps · %s · %s %s (%s)", negotiated.width, negotiated.height,
                                capture::FormatRate(negotiated.rate).c_str(),
                                capture::PixelFormatName(negotiated.format), video::MatrixName(color.matrix),
                                video::RangeName(color.range),
                                video::SourceName(color.matrixSource, color.rangeSource))});
        const auto rect = renderer_.LastVideoRect();
        const double ratio = negotiated.width ? static_cast<double>(rect.width) / negotiated.width : 0.0;
        lines.push_back({T("Display"),
                         Format("%d × %d · %.2fx · %s · %s", rect.width, rect.height, ratio,
                                settings_.presentationMode == settings::PresentationMode::Immediate
                                    ? T("Immediate")
                                    : "VSync",
                                settings_.scalingMode == settings::ScalingMode::Sharp
                                    ? T("Sharp")
                                    : T("Smooth"))});
        lines.push_back({T("Actual FPS"),
                         Format(T("Input %.1f · Present %.1f"), inputFps_, presentFps_)});
        lines.push_back({T("App latency"),
                         Format(T("capture→present %s · copy/decode %s (not total HDMI latency)"),
                                Ms(averageLatencyMs_).c_str(), Ms(averageProcessingMs_).c_str())});
        const auto& counters = capture_.Counters();
        lines.push_back({T("Frames"),
                         Format(T("Input %llu · Presented %llu · Replaced %llu"),
                                U(counters.frames.load()), U(presentedFrames_),
                                U(counters.replaced.load() + counters.skippedInQueue.load()))});
        std::string hdrText;
        if (color.transfer == video::Transfer::Pq) {
            hdrText = hdrActive_
                          ? Format(T("HDR10 input · HDR10 passthrough (%d-bit buffer)"),
                                   framebufferBits_)
                          : Format(T("HDR10 input · SDR tone mapping (%d nit white)"),
                                   settings_.sdrWhiteNits);
        } else {
            hdrText = T("SDR input");
        }
        const ui::HdrSupport support = SessionHdrSupport();
        if (color.transfer == video::Transfer::Pq && !hdrActive_ && !support.output) {
            hdrText += T(" · HDR output not supported");
        } else if (color.transfer != video::Transfer::Pq && !support.input) {
            hdrText += T(" · HDR input not supported");
        }
        if (preferredColor_.known) {
            hdrText += preferredColor_.hdr ? T(" · display HDR")
                                           : T(" · display SDR");
        }
        lines.push_back({"HDR", hdrText});
        lines.push_back({T("Frame loss"),
                         Format(T("Driver dropped %llu · corrupt %llu · %d buffers"),
                                U(counters.driverDropped.load()), U(counters.corrupt.load()), negotiated.bufferCount)});
    }

    const audio::AudioStatus status = audio_.Status();
    if (status.running) {
        lines.push_back({T("Audio output"),
                         Format("SDL3 %s · %s", status.driver.c_str(), status.outputName.c_str())});
        lines.push_back({T("Capture audio"),
                         Format(T("%s · packet %.2f ms"), status.captureName.c_str(),
                                FramesToMs(status.lastCapturePacketFrames))});
        lines.push_back({T("Device buffer"),
                         Format(T("output %.2f ms · input %.2f ms"),
                                FramesToMs(static_cast<uint64_t>(std::max(status.outputDeviceFrames, 0))),
                                FramesToMs(static_cast<uint64_t>(std::max(status.captureDeviceFrames, 0))))});
        lines.push_back({T("App PCM queue"),
                         Format(T("current %.2f ms · target %.2f ms · observed min %.2f ms%s"),
                                FramesToMs(status.ringFrames + status.resamplerFrames), FramesToMs(status.targetFrames),
                                FramesToMs(status.minimumQueueFrames),
                                status.prefilling ? T(" · buffering") : "")});
        lines.push_back({T("Clock drift"),
                         status.driftCorrection
                             ? Format(T("Auto · applied %+d ppm"), status.appliedPpm)
                             : std::string(T("Off · unaltered PCM"))});
        lines.push_back({T("Audio errors"),
                         Format(T("underrun %llu (missing %.2f ms) · overrun %llu · latency trims %llu"),
                                U(status.underrunEvents), FramesToMs(status.underrunFrames), U(status.overrunEvents),
                                U(status.trimEvents))});
    } else {
        std::string text = audioWanted_
                               ? (audioError_.empty() ? std::string(T("Waiting")) : audioError_)
                               : std::string(T("Disabled"));
        lines.push_back({T("Audio"), text});
    }
    lines.push_back({T("Volume"),
                     Format("%d%% · L %d%% · R %d%%%s%s", settings_.volumePercent, settings_.leftVolumePercent,
                            settings_.rightVolumePercent, playbackMuted_ ? T(" · playback muted") : "",
                            settings_.muteWhenBackground && !focused_ ? T(" · muted in background")
                                                                      : "")});
    const recording::Status recordingStatus = recorder_.GetStatus();
    if (recordingStatus.phase != recording::Phase::Idle) {
        std::string text;
        switch (recordingStatus.phase) {
        case recording::Phase::Starting: text = T("Preparing"); break;
        case recording::Phase::Finalizing: text = T("Finalizing"); break;
        default:
            text = Format("%s%s · %s · %s · %d × %d @ %.2f fps · %.1f MB", recordingStatus.paused ? T("Paused · ") : "",
                          Elapsed(recordingStatus.elapsedMs).c_str(), CodecName(settings_.recording.codec),
                          recordingStatus.encoder.c_str(), recordingStatus.width, recordingStatus.height,
                          recordingStatus.fps, static_cast<double>(recordingStatus.bytes) / (1024.0 * 1024.0));
            if (recordingStatus.droppedFrames > 0) {
                text += Format(T(" · dropped %llu"), U(recordingStatus.droppedFrames));
            }
            break;
        }
        lines.push_back({T("Recording"), text});
    }
    return lines;
}

ui::AudioMeterState App::BuildAudioState() const {
    ui::AudioMeterState state;
    state.master = settings_.volumePercent;
    state.left = settings_.leftVolumePercent;
    state.right = settings_.rightVolumePercent;
    state.boost = settings_.allowVolumeBoost;
    state.available = audio_.Running();
    state.muted = settings_.muteWhenBackground && !focused_;
    state.playbackMuted = playbackMuted_;
    state.peakLeft = audio_.PeakLeft();
    state.peakRight = audio_.PeakRight();
    state.clipEvents = audio_.ClipEvents();
    const uint64_t lastClip = audio_.LastClipMs();
    state.clipActive = lastClip != 0 && platform::MonotonicMs() - lastClip < 1500;
    const audio::AudioStatus status = audio_.Status();
    state.captureName = status.running ? status.captureName : captureAudioName_;
    if (status.running) {
        state.outputName = status.outputName;
    } else {
        state.outputName = settings_.audioOutputDevice.empty() ? T("System default output")
                                                               : settings_.audioOutputDevice;
    }
    state.status = audioError_;
    return state;
}

void App::ApplyAudioState(const ui::AudioMeterState& state) {
    settings_.volumePercent = state.master;
    settings_.leftVolumePercent = state.left;
    settings_.rightVolumePercent = state.right;
    ApplyVolume();
    if (state.muteToggled) {
        ToggleMute();
    } else if (state.unmute && playbackMuted_) {
        SetPlaybackMuted(false);
    }
}

ui::SettingsContext App::BuildSettingsContext() {
    ui::SettingsContext context;
    context.captureDevices = &captureDevices_;
    context.recordingDevices = &recordingDevices_;
    context.playbackDevices = &playbackDevices_;
    context.displays = &displays_;
    context.videoDriver = videoDriver_;
    const char* audioDriver = SDL_GetCurrentAudioDriver();
    context.audioDriver = audioDriver ? audioDriver : "";
    context.errorMessage = errorMessage_;
    if (!settingsSaveError_.empty()) {
        if (!context.errorMessage.empty()) context.errorMessage += "\n";
        context.errorMessage += settingsSaveError_;
    }
    context.logDirectory = logDirectoryText_;
    context.screenshotDirectory = screenshotDirectoryText_;
    context.recordingAvailable = recording::RecordingAvailable();
    context.encoders = encoderProbe_.Snapshot();
    const std::filesystem::path recordingDirectory = recording::RecordingDirectory(settings_.recording);
    context.recordingDirectory = recordingDirectory.string();
    context.recordingFreeBytes = recording::FreeBytes(recordingDirectory);
    context.lastRecording = lastRecordingText_;
    context.canCancel = settingsBeforeEdit_.has_value();
    context.fonts = fonts_;
    context.scale = scale_;
    context.framebufferBits = framebufferBits_;

    const capture::CaptureDevice* device = capture::SelectDevice(captureDevices_, settings_.captureDevice);
    context.hdr = EvaluateHdrSupport(device);
    if (device) {
        context.autoCaptureAudio =
            audio::MatchCaptureAudioDevice(recordingDevices_, device->usbProduct, device->card);
    }
    const std::string audioName = ResolveCaptureAudio(device);
    if (settings_.audioOnly) {
        context.canStart = !audioName.empty();
        context.startSummary = context.canStart
                                   ? Format(T("Audio only · %s"), audioName.c_str())
                                   : std::string(T("Select a capture audio device."));
    } else if (!device) {
        context.startSummary = T("No capture device.");
    } else if (const auto mode = capture::ResolveMode(*device, RequestFromSettings(settings_))) {
        context.canStart = true;
        context.startSummary =
            Format("%s · %d × %d · %s · %s fps%s · %s", device->Name().c_str(), mode->width, mode->height,
                   capture::PixelFormatName(mode->format), capture::FormatRate(mode->rate).c_str(),
                   mode->directRequest ? T(" (direct request)") : "",
                   audioName.empty() ? T("no audio")
                                     : T("with audio"));
    } else {
        context.startSummary = T("The device does not support the selected mode.");
    }
    return context;
}

void App::RefreshDevices() {
    captureDevices_ = capture::EnumerateCaptureDevices();
    RefreshAudioDevices();
    RefreshDisplays();
    Log("[devices] %zu capture, %zu recording, %zu playback, %zu displays", captureDevices_.size(),
        recordingDevices_.size(), playbackDevices_.size(), displays_.size());
}

void App::RefreshAudioDevices() {
    recordingDevices_ = audio::RecordingDevices();
    playbackDevices_ = audio::PlaybackDevices();
}

void App::RefreshDisplays() {
    displays_.clear();
    int count = 0;
    if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
        for (int i = 0; i < count; ++i) {
            const char* name = SDL_GetDisplayName(ids[i]);
            displays_.push_back({ids[i], name && *name ? std::string(name) : Format("Display %d", i + 1)});
        }
        SDL_free(ids);
    }
}

std::string App::ResolveCaptureAudio(const capture::CaptureDevice* device) const {
    if (settings_.captureAudioDevice == settings::kCaptureAudioDisabled) return {};
    if (!settings_.captureAudioDevice.empty()) {
        return audio::HasDevice(recordingDevices_, settings_.captureAudioDevice) ? settings_.captureAudioDevice
                                                                                 : std::string{};
    }
    if (!device) return {};
    return audio::MatchCaptureAudioDevice(recordingDevices_, device->usbProduct, device->card);
}

bool App::StartSession(std::string& error) {
    errorMessage_.clear();
    RefreshDevices();
    const capture::CaptureDevice* device = capture::SelectDevice(captureDevices_, settings_.captureDevice);
    activeMode_.reset();
    if (!settings_.audioOnly) {
        if (!device) {
            error = T("No capture device was found. Check the USB connection.");
            return false;
        }
        activeMode_ = capture::ResolveMode(*device, RequestFromSettings(settings_));
        if (activeMode_ && activeMode_->format == capture::PixelFormat::P010 &&
            !renderer_.SupportsSixteenBitTextures()) {
            error = Format(T("HDR input not supported: %s"),
                           EvaluateHdrSupport(device).inputText.c_str());
            activeMode_.reset();
            return false;
        }
        if (!activeMode_) {
            if (settings_.pixelFormat == settings::PixelFormatChoice::P010 &&
                !device->Supports(capture::PixelFormat::P010)) {
                error = Format(T("HDR input not supported: %s"),
                               EvaluateHdrSupport(device).inputText.c_str());
            } else {
                error = T("The device does not support the selected resolution and format.");
            }
            return false;
        }
    }
    captureAudioName_ = ResolveCaptureAudio(device);
    audioWanted_ = !captureAudioName_.empty();
    if (settings_.audioOnly && !audioWanted_) {
        error = T("No capture audio device was found. Select one under Audio → Capture audio device.");
        return false;
    }

    if (activeMode_) {
        capture::CaptureConfig config;
        config.devicePath = device->id;
        config.format = activeMode_->format;
        config.width = activeMode_->width;
        config.height = activeMode_->height;
        config.rate = activeMode_->rate;
        config.capture40Fps = settings_.capture40Fps;
        config.directRate = activeMode_->directRequest;
        config.colorOverride = settings_.colorOverride;
        config.hdrInput = settings_.hdrInput;
        switch (settings_.hdrChromaLocation) {
        case settings::HdrChromaLocation::Left: renderer_.SetP010ChromaSiting(video::ChromaSiting::Left); break;
        case settings::HdrChromaLocation::TopLeft: renderer_.SetP010ChromaSiting(video::ChromaSiting::TopLeft); break;
        case settings::HdrChromaLocation::Auto: renderer_.SetP010ChromaSiting(video::ChromaSiting::Center); break;
        }
        std::string captureError;
        if (!capture_.Start(config, mailbox_, [this] { NotifyFrame(); }, captureError)) {
            error = std::string(T("Could not start capture: ")) + captureError;
            if (settings_.capture40Fps) {
                error += T("\n\n40 fps capture is enabled. This device, resolution or format may reject 40 fps or "
                           "report a different rate. Uncheck Video → 40 fps capture to retry with the normal frame-rate "
                           "selection. The diagnostic log tells a format rejection apart from other startup errors.");
            } else if (activeMode_->directRequest) {
                error += T("\n\n30 fps was requested directly because the driver does not list it. The driver rejected "
                           "it or applied another rate. Choose a listed frame rate or turn off Video → Allow unlisted "
                           "FPS.");
            }
            return false;
        }
        activeDeviceName_ = device->Name();
        activeDevice_ = *device;
    } else {
        activeDeviceName_.clear();
        activeDevice_.reset();
    }

    audioError_.clear();
    audioRetryMs_ = 0;
    if (audioWanted_) {
        std::string audioError;
        if (!StartAudio(audioError)) {
            if (settings_.audioOnly) {
                capture_.Stop();
                error = std::string(T("Could not start audio: ")) + audioError;
                return false;
            }
            audioError_ = audioError;
            audioRetryMs_ = platform::MonotonicMs() + 2000;
            ShowToast(std::string(T("Could not start audio: ")) + audioError, true);
        }
    } else if (!settings_.audioOnly && settings_.captureAudioDevice != settings::kCaptureAudioDisabled) {
        ShowToast(T("No capture audio device was found; playing video only."),
                  true);
    }

    renderer_.Reset();
    newVideoFrame_ = false;
    presentedFrames_ = 0;
    statsWindowStartMs_ = 0;
    statsCapturedBase_ = 0;
    statsPresentedBase_ = 0;
    statsProcessingNsBase_ = 0;
    statsProcessingSamplesBase_ = 0;
    latencySumMs_ = 0.0;
    latencySamples_ = 0;
    inputFps_ = 0.0;
    presentFps_ = 0.0;
    averageLatencyMs_ = -1.0;
    averageProcessingMs_ = -1.0;
    sessionStartMs_ = platform::MonotonicMs();
    lastMouseMoveMs_ = sessionStartMs_;
    noSignalShown_ = false;
    lastCaptureState_ = capture_.State();
    helpVisible_ = false;
    screen_ = settings_.audioOnly ? Screen::AudioOnly : Screen::Video;
    ConfigureSessionWindow();
    ApplySwapInterval();
    RefreshPreferredColor();
    UpdateHdrState();
    if (activeMode_ && capture_.Negotiated().color.transfer == video::Transfer::Pq && !hdrActive_) {
        const ui::HdrSupport support = SessionHdrSupport();
        if (!support.output) {
            ShowToast(Format(T("HDR output not supported: %s · showing as SDR."),
                             support.outputText.c_str()),
                      true);
        }
    } else if (toastUntilMs_ < platform::MonotonicMs()) {
        ShowToast(T("Press Alt to show the menu."));
    }
    UpdateBackgroundMute();
    UpdateSleepPrevention();
    SaveSettings();
    if (activeMode_) {
        Log("[session] video %s %dx%d %s @ %s fps, audio '%s'", activeDeviceName_.c_str(), activeMode_->width,
            activeMode_->height, capture::PixelFormatName(activeMode_->format),
            capture::FormatRate(activeMode_->rate).c_str(), captureAudioName_.c_str());
    } else {
        Log("[session] audio only '%s'", captureAudioName_.c_str());
    }
    return true;
}

void App::StopSession() {
    if (screen_ == Screen::Settings) return;
    if (recorder_.Busy()) {
        recorder_.Stop();
        if (!recorder_.WaitIdle(15000)) Log("[recording] finalizing is taking longer than expected");
        PollRecording();
    }
    confirmExit_ = ExitRequest::None;
    sleepInhibitor_.Release();
    capture_.Stop();
    audio_.Stop();
    audioWanted_ = false;
    audioRetryMs_ = 0;
    hdrOutput_.Disable();
    hdrActive_ = false;
    renderer_.Configure(video::OutputMode::Sdr, static_cast<float>(settings_.sdrWhiteNits));
    renderer_.Reset();
    mailbox_.Reset();
    newVideoFrame_ = false;
    framePending_.store(false, std::memory_order_release);
    if (fullscreen_) SetFullscreen(false);
    if (cursorHidden_) {
        SDL_ShowCursor();
        cursorHidden_ = false;
    }
    audioOsdVisible_ = false;
    helpVisible_ = false;
    menuVisible_ = false;
    SaveSettings();
    Log("[session] stopped");
}

void App::EnterSettings() {
    screen_ = Screen::Settings;
    settingsScreen_.ResetRecordingPage();
    SDL_SetWindowHitTest(window_, nullptr, nullptr);
    if (fullscreen_) SetFullscreen(false);
    SDL_SetWindowAspectRatio(window_, 0.0f, 0.0f);
    SDL_SetWindowBordered(window_, true);
    SDL_SetWindowResizable(window_, true);
    SDL_SetWindowMinimumSize(window_, 760, 520);
    SDL_SetWindowSize(window_, kSettingsWidth, kSettingsHeight);
    SDL_SetWindowTitle(window_, T("LLCV Settings"));
    SDL_ShowWindow(window_);
    SDL_RaiseWindow(window_);
    ApplySwapInterval();
    UpdateBackgroundMute();
    RefreshDevices();
    RefreshPreferredColor();
    redrawFrames_ = std::max(redrawFrames_, 3);
}

bool App::StartAudio(std::string& error) {
    audio::AudioConfig config;
    config.captureDevice = captureAudioName_;
    config.outputDevice = settings_.audioOutputDevice;
    config.deviceFrames = settings_.audioDeviceFrames;
    config.targetMs = settings_.pcmQueueTargetMs;
    config.driftCorrection = settings_.driftCorrection == settings::DriftCorrection::Auto;
    ApplyVolume();
    UpdateBackgroundMute();
    audio_.SetPlaybackMuted(playbackMuted_);
    return audio_.Start(config, error);
}

void App::ConfigureSessionWindow() {
    SDL_SetWindowTitle(window_, kAppName);
    if (screen_ == Screen::AudioOnly) {
        const float aspect = std::clamp(static_cast<float>(settings_.audioOnlyWidth) /
                                            static_cast<float>(std::max(settings_.audioOnlyHeight, 1)),
                                        0.5f, 4.0f);
        SDL_SetWindowAspectRatio(window_, 0.0f, 0.0f);
        SDL_SetWindowBordered(window_, !settings_.borderlessWindow);
        SDL_SetWindowResizable(window_, true);
        SDL_SetWindowMinimumSize(window_, 320, 220);
        SDL_SetWindowSize(window_, settings_.audioOnlyWidth, settings_.audioOnlyHeight);
        SDL_SetWindowAspectRatio(window_, aspect, aspect);
        SDL_SetWindowHitTest(window_, &App::AudioOnlyHitTest, this);
        SDL_ShowWindow(window_);
        SDL_RaiseWindow(window_);
        return;
    }
    SDL_SetWindowHitTest(window_, nullptr, nullptr);
    SDL_SetWindowMinimumSize(window_, 160, 90);
    SDL_SetWindowBordered(window_, !settings_.borderlessWindow);
    SDL_ShowWindow(window_);
    SDL_SyncWindow(window_);
    PositionOnPreferredDisplay();
    ApplyVideoWindowSize(true);
    SDL_RaiseWindow(window_);
    if (settings_.startFullscreen || CaptureMatchesDisplay()) SetFullscreen(true);
    RememberWindowDisplay();
}

bool App::ApplyVideoWindowSize(bool useSavedSize) {
    if (!activeMode_ || !window_) return true;
    const float density = std::max(SDL_GetWindowPixelDensity(window_), 0.25f);
    float width = static_cast<float>(activeMode_->width) / density;
    float height = static_cast<float>(activeMode_->height) / density;
    if (useSavedSize && !settings_.pixelPerfect && settings_.windowWidth > 0) {
        width = static_cast<float>(settings_.windowWidth);
        height = width * static_cast<float>(activeMode_->height) / static_cast<float>(activeMode_->width);
    }

    SDL_Rect usable{0, 0, 0, 0};
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    if (!display || !SDL_GetDisplayUsableBounds(display, &usable) || usable.w <= 0 || usable.h <= 0) {
        usable.w = 3840;
        usable.h = 2160;
    }
    const float decoration = settings_.borderlessWindow ? 0.0f : 48.0f;
    const float maximumWidth = static_cast<float>(usable.w);
    const float maximumHeight = std::max(90.0f, static_cast<float>(usable.h) - decoration);
    const float fit = std::min({1.0f, maximumWidth / width, maximumHeight / height});
    const int finalWidth = std::max(160, static_cast<int>(std::lround(width * fit)));
    const int finalHeight = std::max(90, static_cast<int>(std::lround(height * fit)));

    SDL_SetWindowResizable(window_, true);
    SDL_SetWindowAspectRatio(window_, 0.0f, 0.0f);
    SDL_SetWindowSize(window_, finalWidth, finalHeight);
    ApplyWindowConstraints();
    return fit >= 0.999f;
}

void App::ApplyWindowConstraints() {
    if (screen_ != Screen::Video || !activeMode_ || fullscreen_ || !window_) return;
    const float aspect = static_cast<float>(activeMode_->width) / static_cast<float>(activeMode_->height);
    if (settings_.pixelPerfect) {
        SDL_SetWindowAspectRatio(window_, 0.0f, 0.0f);
        SDL_SetWindowResizable(window_, false);
    } else {
        SDL_SetWindowResizable(window_, true);
        SDL_SetWindowAspectRatio(window_, aspect, aspect);
    }
}

void App::PositionOnPreferredDisplay() {
    if (!settings_.preferredDisplay.empty()) {
        for (const auto& display : displays_) {
            if (display.name != settings_.preferredDisplay) continue;
            SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED_DISPLAY(display.id),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(display.id));
            return;
        }
    }
    if (settings_.hasWindowPosition && !IsWayland()) {
        SDL_SetWindowPosition(window_, settings_.windowX, settings_.windowY);
    }
}

void App::SetFullscreen(bool fullscreen) {
    if (!window_ || (fullscreen && screen_ == Screen::Settings)) return;
    if (fullscreen) {
        SDL_SetWindowAspectRatio(window_, 0.0f, 0.0f);
        SDL_SetWindowResizable(window_, true);
    }
    SDL_SetWindowFullscreen(window_, fullscreen);
    fullscreen_ = fullscreen;
    lastMouseMoveMs_ = platform::MonotonicMs();
    if (!fullscreen) ApplyWindowConstraints();
}

void App::ApplySwapInterval() {
    const bool immediate =
        screen_ == Screen::Video && settings_.presentationMode == settings::PresentationMode::Immediate;
    if (!SDL_GL_SetSwapInterval(immediate ? 0 : 1)) Log("[gl] swap interval failed: %s", SDL_GetError());
}

void App::RefreshPreferredColor() {
    if (!hdrOutput_.Supported()) return;
    preferredColor_ = hdrOutput_.QueryPreferred();
    Log("[hdr] preferred: known %d, hdr %d, tf %u, max %u, reference %u, target max %u", preferredColor_.known,
        preferredColor_.hdr, preferredColor_.transfer, preferredColor_.maxLuminance,
        preferredColor_.referenceLuminance, preferredColor_.targetMaxLuminance);
}

ui::HdrSupport App::EvaluateHdrSupport(const capture::CaptureDevice* device) const {
    ui::HdrSupport support;
    if (!device) {
        support.inputText = T("No capture device.");
    } else if (device->Supports(capture::PixelFormat::P010) && !renderer_.SupportsSixteenBitTextures()) {
        support.inputText = T("The graphics driver (OpenGL ES) lacks 16-bit textures (EXT_texture_norm16), so P010 cannot be shown.");
    } else if (device->Supports(capture::PixelFormat::P010)) {
        support.input = true;
        support.inputText = T("P010 (10-bit HDR10) is available · select P010 as the pixel format.");
    } else if (device->advertisesP010) {
        support.inputText = Format(T("The device offers P010 (HDR10), but uvcvideo in kernel %s does not recognize it. "
                                     "Linux 7.1 or newer, or a P010 DKMS module, is required."),
                                   kernelRelease_.c_str());
    } else {
        support.inputText = T("The device offers no 10-bit HDR format. 8-bit HDR signals can use 'Force HDR10'.");
    }

    if (forceSdr_) {
        support.outputText = T("HDR output is off because LLCV was started with --sdr.");
    } else if (!video::HdrOutput::Compiled()) {
        support.outputText = T("This build lacks Wayland color management.");
    } else if (!IsWayland()) {
        support.outputText = T("HDR output is unavailable on X11. Run on Wayland.");
    } else if (!hdrOutput_.Connected()) {
        support.outputText = T("The compositor lacks Wayland color management (wp_color_management_v1). "
                               "GNOME 48 or newer, or KDE Plasma 6, is required.");
    } else if (!hdrOutput_.Supported()) {
        support.outputText = T("The compositor does not support PQ/BT.2020 HDR10.");
    } else if (framebufferBits_ < 10) {
        support.outputText = T("The graphics driver provides no 10-bit framebuffer. "
                               "'Always HDR output' sends 8-bit HDR instead.");
    } else if (preferredColor_.known && !preferredColor_.hdr) {
        support.output = true;
        support.outputText = T("Supported · HDR is off on this display. Turn it on in the display settings.");
    } else {
        support.output = true;
        support.outputText = Format(T("Supported · %d-bit framebuffer · Wayland color management"),
                                    framebufferBits_);
    }
    return support;
}

ui::HdrSupport App::SessionHdrSupport() const {
    return EvaluateHdrSupport(activeDevice_ ? &*activeDevice_ : nullptr);
}

void App::UpdateHdrState() {
    bool wanted = false;
    const bool sourcePq = screen_ == Screen::Video && activeMode_ &&
                          capture_.Negotiated().color.transfer == video::Transfer::Pq;
    if (sourcePq && hdrOutput_.Supported()) {
        switch (settings_.hdrOutput) {
        case settings::HdrOutputMode::Auto: wanted = framebufferBits_ >= 10 && preferredColor_.hdr; break;
        case settings::HdrOutputMode::Hdr: wanted = true; break;
        case settings::HdrOutputMode::ToneMap: break;
        }
    }
    if (wanted && !hdrActive_) {
        hdrActive_ = hdrOutput_.Enable();
        if (!hdrActive_) ShowToast(T("HDR output failed; using SDR tone mapping."), true);
    } else if (!wanted && hdrActive_) {
        hdrOutput_.Disable();
        hdrActive_ = false;
    }
    renderer_.Configure(hdrActive_ ? video::OutputMode::Pq : video::OutputMode::Sdr,
                        static_cast<float>(settings_.sdrWhiteNits));
    redrawFrames_ = std::max(redrawFrames_, 1);
}

void App::ApplyLanguage() {
    switch (settings_.uiLanguage) {
    case settings::UiLanguage::Korean: ui::SetLanguage(ui::Language::Korean); break;
    case settings::UiLanguage::English: ui::SetLanguage(ui::Language::English); break;
    case settings::UiLanguage::Japanese: ui::SetLanguage(ui::Language::Japanese); break;
    case settings::UiLanguage::Spanish: ui::SetLanguage(ui::Language::Spanish); break;
    case settings::UiLanguage::Portuguese: ui::SetLanguage(ui::Language::Portuguese); break;
    case settings::UiLanguage::Auto: ui::SetLanguage(ui::SystemLanguage()); break;
    }
    if (window_ && screen_ == Screen::Settings) {
        SDL_SetWindowTitle(window_, T("LLCV Settings"));
    }
}

void App::ApplyStyle() {
    float scale = window_ ? SDL_GetWindowDisplayScale(window_) : 1.0f;
    if (scale <= 0.0f) scale = 1.0f;
    scale_ = std::clamp(scale, 0.5f, 4.0f);
    ui::ApplyTheme(settings_.lightTheme, scale_);
}

void App::ApplyVolume() {
    const int maximum = settings_.allowVolumeBoost ? 200 : 100;
    settings_.volumePercent = std::clamp(settings_.volumePercent, 0, maximum);
    settings_.leftVolumePercent = std::clamp(settings_.leftVolumePercent, 0, 100);
    settings_.rightVolumePercent = std::clamp(settings_.rightVolumePercent, 0, 100);
    audio_.SetVolume(settings_.volumePercent, settings_.leftVolumePercent, settings_.rightVolumePercent);
}

void App::UpdateBackgroundMute() {
    audio_.SetBackgroundMuted(settings_.muteWhenBackground && !focused_ && screen_ != Screen::Settings);
}

void App::ChangeVolume(int delta) {
    const int maximum = settings_.allowVolumeBoost ? 200 : 100;
    int value = std::clamp(settings_.volumePercent + delta, 0, maximum);
    value = std::min(maximum, (value + 2) / 5 * 5);
    settings_.volumePercent = value;
    ApplyVolume();
    if (playbackMuted_) SetPlaybackMuted(false);
    volumeHudUntilMs_ = platform::MonotonicMs() + kVolumeHudMs;
}

void App::RememberWindowGeometry() {
    if (fullscreen_ || !window_) return;
    if (SDL_GetWindowFlags(window_) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) return;
    if (screen_ == Screen::AudioOnly) {
        int width = 0;
        int height = 0;
        if (SDL_GetWindowSize(window_, &width, &height) && width > 0 && height > 0) {
            settings_.audioOnlyWidth = width;
            settings_.audioOnlyHeight = height;
        }
        return;
    }
    if (screen_ != Screen::Video) return;
    if (!settings_.pixelPerfect) {
        int width = 0;
        int height = 0;
        if (SDL_GetWindowSize(window_, &width, &height) && width > 0 && height > 0) {
            settings_.windowWidth = width;
            settings_.windowHeight = height;
        }
    }
    if (!IsWayland()) {
        int x = 0;
        int y = 0;
        if (SDL_GetWindowPosition(window_, &x, &y)) {
            settings_.windowX = x;
            settings_.windowY = y;
            settings_.hasWindowPosition = true;
        }
    }
}

void App::RequestScreenshot() {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    if (!renderer_.ReadImage(rgba, width, height)) {
        ShowToast(T("Screenshot failed · no video frame yet."), true);
        return;
    }
    screenshots_.Request(std::move(rgba), width, height, platform::ScreenshotDirectory(),
                         settings_.screenshotClipboard);
    ShowToast(T("Saving screenshot…"));
}

void App::PollScreenshots() {
    while (auto result = screenshots_.Poll()) {
        if (!result->ok) {
            Log("[screenshot] failed: %s", result->error.c_str());
            ShowToast(Format(T("Screenshot failed: %s"), result->error.c_str()), true);
            continue;
        }
        Log("[screenshot] saved %s", result->path.c_str());
        std::string message = Format(T("Screenshot saved: %s"), result->path.filename().c_str());
        if (renderer_.SourceIsPq()) message += T(" (HDR converted to SDR)");
        if (!result->png.empty()) {
            CopyPngToClipboard(std::move(result->png));
            message += T(" · copied to clipboard");
        }
        ShowToast(message);
    }
}

void App::CopyPngToClipboard(std::vector<uint8_t> png) {
    static const char* kMimeTypes[] = {"image/png"};
    auto* payload = new ClipboardPng{std::move(png)};
    if (!SDL_SetClipboardData(ProvideClipboard, ReleaseClipboard, payload, kMimeTypes, 1)) {
        Log("[screenshot] clipboard failed: %s", SDL_GetError());
    }
}

void App::ShowToast(std::string text, bool error) {
    toastText_ = std::move(text);
    toastError_ = error;
    toastUntilMs_ = platform::MonotonicMs() + (error ? 4000 : 2500);
    redrawFrames_ = std::max(redrawFrames_, 1);
}

void App::OpenFolder(const std::filesystem::path& path) {
    platform::EnsureDirectory(path);
    if (SDL_OpenURL(platform::FileUrl(path).c_str())) return;
    const std::string message = Format(T("Could not open the folder: %s"), path.c_str());
    if (screen_ == Screen::Settings) {
        errorMessage_ = message;
    } else {
        ShowToast(message, true);
    }
}

void App::DrawMenu() {
    ui::MenuState state;
    state.audioOnly = screen_ == Screen::AudioOnly;
    state.fullscreen = fullscreen_;
    state.borderless = settings_.borderlessWindow;
    state.sharp = settings_.scalingMode == settings::ScalingMode::Sharp;
    state.vsync = settings_.presentationMode == settings::PresentationMode::VSync;
    state.diagnostics = diagnosticsVisible_;
    state.audioOsd = audioOsdVisible_;
    state.backgroundMute = settings_.muteWhenBackground;
    state.playbackMuted = playbackMuted_;
    const recording::Status recordingStatus = recorder_.GetStatus();
    state.recording = recordingStatus.phase == recording::Phase::Recording;
    state.recordingPaused = state.recording && recordingStatus.paused;
    state.recordingBusy =
        recordingStatus.phase == recording::Phase::Starting || recordingStatus.phase == recording::Phase::Finalizing;
    state.lastRecording = !lastRecordingPath_.empty();
    state.volume = settings_.volumePercent;
    const ui::MenuCommand command = ui::DrawMenuBar(state, settings_.shortcuts, scale_);
    if (command != ui::MenuCommand::None) pendingMenuCommand_ = command;
}

void App::ExecuteMenuCommand(ui::MenuCommand command) {
    menuVisible_ = false;
    if (screen_ == Screen::Settings) return;
    switch (command) {
    case ui::MenuCommand::Fullscreen:
        SetFullscreen(!fullscreen_);
        break;
    case ui::MenuCommand::RestoreSize:
        RestoreOneToOne();
        break;
    case ui::MenuCommand::Borderless:
        settings_.borderlessWindow = !settings_.borderlessWindow;
        SDL_SetWindowBordered(window_, !settings_.borderlessWindow);
        break;
    case ui::MenuCommand::ScalingSmooth:
        settings_.scalingMode = settings::ScalingMode::Smooth;
        break;
    case ui::MenuCommand::ScalingSharp:
        settings_.scalingMode = settings::ScalingMode::Sharp;
        break;
    case ui::MenuCommand::PresentImmediate:
        settings_.presentationMode = settings::PresentationMode::Immediate;
        ApplySwapInterval();
        break;
    case ui::MenuCommand::PresentVSync:
        settings_.presentationMode = settings::PresentationMode::VSync;
        ApplySwapInterval();
        break;
    case ui::MenuCommand::Diagnostics:
        diagnosticsVisible_ = !diagnosticsVisible_;
        if (diagnosticsVisible_) audio_.ResetMinimum();
        break;
    case ui::MenuCommand::AudioOsd:
        if (screen_ == Screen::Video) audioOsdVisible_ = !audioOsdVisible_;
        break;
    case ui::MenuCommand::VolumeUp:
        ChangeVolume(5);
        break;
    case ui::MenuCommand::VolumeDown:
        ChangeVolume(-5);
        break;
    case ui::MenuCommand::BackgroundMute:
        settings_.muteWhenBackground = !settings_.muteWhenBackground;
        UpdateBackgroundMute();
        break;
    case ui::MenuCommand::Mute:
        ToggleMute();
        break;
    case ui::MenuCommand::Recording:
        ToggleRecording();
        break;
    case ui::MenuCommand::RecordingPause:
        ToggleRecordingPause();
        break;
    case ui::MenuCommand::OpenRecordings:
        OpenFolder(recording::RecordingDirectory(settings_.recording));
        break;
    case ui::MenuCommand::OpenLastRecording:
        OpenLastRecording();
        break;
    case ui::MenuCommand::Screenshot:
        if (screen_ == Screen::Video) RequestScreenshot();
        break;
    case ui::MenuCommand::OpenScreenshots:
        OpenFolder(platform::ScreenshotDirectory());
        break;
    case ui::MenuCommand::OpenLogs:
        OpenFolder(platform::LogDirectory());
        break;
    case ui::MenuCommand::Settings:
        RequestSettings();
        break;
    case ui::MenuCommand::Help:
        helpVisible_ = true;
        break;
    case ui::MenuCommand::Quit:
        RequestQuit();
        break;
    case ui::MenuCommand::None:
        break;
    }
}

void App::SaveSettings() {
    if (settings::SaveSettings(settingsPath_, settings_)) {
        settingsSaveError_.clear();
        return;
    }
    Log("[settings] could not save %s", settingsPath_.c_str());
    settingsSaveError_ = Format(T("Settings could not be saved. The previous settings file is kept: %s"),
                                settingsPath_.c_str());
    if (screen_ != Screen::Settings) ShowToast(settingsSaveError_, true);
}

bool App::RecordingActive() const {
    const recording::Phase phase = recorder_.GetStatus().phase;
    return phase == recording::Phase::Starting || phase == recording::Phase::Recording;
}

void App::RequestQuit() {
    if (screen_ != Screen::Settings && RecordingActive()) {
        confirmExit_ = ExitRequest::Quit;
        menuVisible_ = false;
        helpVisible_ = false;
        redrawFrames_ = std::max(redrawFrames_, 2);
        return;
    }
    running_ = false;
}

void App::RequestSettings() {
    if (RecordingActive()) {
        confirmExit_ = ExitRequest::Settings;
        menuVisible_ = false;
        helpVisible_ = false;
        redrawFrames_ = std::max(redrawFrames_, 2);
        return;
    }
    PerformExit(ExitRequest::Settings);
}

void App::PerformExit(ExitRequest request) {
    confirmExit_ = ExitRequest::None;
    switch (request) {
    case ExitRequest::Quit:
        running_ = false;
        break;
    case ExitRequest::Settings:
        StopSession();
        settingsBeforeEdit_ = settings_;
        EnterSettings();
        break;
    case ExitRequest::None:
        break;
    }
}

void App::DrawRecordingConfirm() {
    switch (ui::DrawRecordingConfirm(confirmExit_ == ExitRequest::Settings, settings_.shortcuts, fonts_, scale_)) {
    case ui::ConfirmResult::Confirm:
        pendingExit_ = confirmExit_;
        confirmExit_ = ExitRequest::None;
        break;
    case ui::ConfirmResult::Cancel:
        confirmExit_ = ExitRequest::None;
        break;
    case ui::ConfirmResult::None:
        break;
    }
}

void App::ToggleRecording() {
    const recording::Phase phase = recorder_.GetStatus().phase;
    if (phase == recording::Phase::Recording) {
        StopRecording();
    } else if (phase == recording::Phase::Idle) {
        StartRecording();
    }
}

void App::StartRecording() {
    if (screen_ != Screen::Video || !activeMode_) {
        ShowToast(T("Recording is available in video mode."), true);
        return;
    }
    const capture::NegotiatedFormat negotiated = capture_.Negotiated();
    recording::StartRequest request;
    request.options = settings_.recording;
    request.video.format = negotiated.format;
    request.video.width = negotiated.width;
    request.video.height = negotiated.height;
    request.video.rate = negotiated.rate;
    request.video.color = negotiated.color;
    request.audio = audioWanted_;
    request.folder = recording::RecordingDirectory(settings_.recording);
    const recording::StartError error = recorder_.Start(request);
    if (error != recording::StartError::None) {
        Log("[recording] not started (%d)", static_cast<int>(error));
        ShowToast(StartErrorText(error), true);
        return;
    }
    ShowToast(input::ExpandKeyLabels(T("Recording started · press F9 again to stop"), settings_.shortcuts));
}

void App::StopRecording() {
    recorder_.Stop();
    ShowToast(T("Finalizing recording…"));
}

void App::ToggleRecordingPause() {
    const recording::Status status = recorder_.GetStatus();
    if (status.phase != recording::Phase::Recording) return;
    recorder_.SetPaused(!status.paused);
    ShowToast(status.paused ? T("Recording resumed.") : T("Recording paused. Playback continues."));
}

void App::PollRecording() {
    while (auto event = recorder_.PollEvent()) {
        std::string message;
        bool error = false;
        switch (event->kind) {
        case recording::Event::Kind::Saved:
            message = Format(T("Recording saved: %s"), event->path.filename().c_str());
            if (event->segments > 1) message += Format(T(" (%d files)"), event->segments);
            lastRecordingText_ = event->path.string();
            lastRecordingPath_ = event->path;
            break;
        case recording::Event::Kind::Failed:
            error = true;
            if (event->fileKept) {
                message = FailureText(event->failure) + " " + T("The part before the stop was saved.");
                lastRecordingText_ = event->path.string();
                lastRecordingPath_ = event->path;
            } else if (event->failure == recording::Failure::EncoderFailed && !event->detail.empty()) {
                message = Format(T("Recording could not start: %s"), event->detail.c_str());
            } else {
                message = FailureText(event->failure);
            }
            break;
        case recording::Event::Kind::Empty:
            error = true;
            message = T("Nothing was recorded.");
            break;
        }
        Log("[recording] %s", message.c_str());
        ShowToast(message, error);
    }
}

void App::ToggleMute() {
    SetPlaybackMuted(!playbackMuted_);
    volumeHudUntilMs_ = platform::MonotonicMs() + kVolumeHudMs;
}

void App::SetPlaybackMuted(bool muted) {
    playbackMuted_ = muted;
    audio_.SetPlaybackMuted(muted);
    redrawFrames_ = std::max(redrawFrames_, 1);
}

void App::ChooseRecordingFolder() {
    std::filesystem::path start = recording::RecordingDirectory(settings_.recording);
    std::error_code error;
    while (!start.empty() && !std::filesystem::is_directory(start, error)) {
        const std::filesystem::path parent = start.parent_path();
        if (parent == start) break;
        start = parent;
    }
    const std::string location = start.string();
    SDL_ShowOpenFolderDialog(&App::OnRecordingFolderChosen, this, window_, location.empty() ? nullptr : location.c_str(),
                             false);
}

void SDLCALL App::OnRecordingFolderChosen(void* userdata, const char* const* files, int) {
    auto* app = static_cast<App*>(userdata);
    std::lock_guard<std::mutex> lock(app->folderMutex_);
    if (!files) {
        const char* error = SDL_GetError();
        app->folderError_ = error && *error ? error : "unknown error";
        return;
    }
    if (files[0] && *files[0]) app->chosenFolder_ = std::string(files[0]);
}

void App::ApplyPlacement() {
    video::Placement placement;
    placement.fullscreen = fullscreen_ && screen_ == Screen::Video;
    placement.originalSize = settings_.fullscreenVideoSize == settings::FullscreenVideoSize::Original;
    switch (settings_.fullscreenVideoPosition) {
    case settings::FullscreenVideoPosition::TopLeft: placement.horizontal = 0; placement.vertical = 0; break;
    case settings::FullscreenVideoPosition::Top: placement.vertical = 0; break;
    case settings::FullscreenVideoPosition::TopRight: placement.horizontal = 2; placement.vertical = 0; break;
    case settings::FullscreenVideoPosition::Left: placement.horizontal = 0; break;
    case settings::FullscreenVideoPosition::Right: placement.horizontal = 2; break;
    case settings::FullscreenVideoPosition::BottomLeft: placement.horizontal = 0; placement.vertical = 2; break;
    case settings::FullscreenVideoPosition::Bottom: placement.vertical = 2; break;
    case settings::FullscreenVideoPosition::BottomRight: placement.horizontal = 2; placement.vertical = 2; break;
    case settings::FullscreenVideoPosition::Center: break;
    }
    renderer_.SetPlacement(placement);
}

bool App::CaptureMatchesDisplay() const {
    if (!activeMode_ || !window_) return false;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    const SDL_DisplayMode* mode = display ? SDL_GetDesktopDisplayMode(display) : nullptr;
    if (!mode) return false;
    const float density = mode->pixel_density > 0.0f ? mode->pixel_density : 1.0f;
    const int width = static_cast<int>(std::lround(mode->w * density));
    const int height = static_cast<int>(std::lround(mode->h * density));
    return width == activeMode_->width && height == activeMode_->height;
}

void App::RestoreOneToOne() {
    if (screen_ != Screen::Video || !activeMode_) return;
    if (CaptureMatchesDisplay()) {
        if (!fullscreen_) SetFullscreen(true);
        ShowToast(T("1:1 size · the capture matches this monitor, so fullscreen is used."));
        return;
    }
    if (fullscreen_) SetFullscreen(false);
    if (!ApplyVideoWindowSize(false)) {
        ShowToast(T("A 1:1 window does not fit this monitor, so it was reduced to fit."), true);
    }
    RememberWindowDisplay();
}

void App::RememberWindowDisplay() {
    if (!window_) return;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    SDL_Rect bounds{0, 0, 0, 0};
    if (!display || !SDL_GetDisplayUsableBounds(display, &bounds) || bounds.w <= 0 || bounds.h <= 0) return;
    windowDisplay_ = display;
    windowDisplayBounds_ = bounds;
}

void App::FollowDisplayChange() {
    if (!window_) return;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    SDL_Rect bounds{0, 0, 0, 0};
    if (!display || !SDL_GetDisplayUsableBounds(display, &bounds) || bounds.w <= 0 || bounds.h <= 0) return;
    const bool moved = windowDisplay_ != 0 && display != windowDisplay_ && windowDisplayBounds_.w > 0 &&
                       windowDisplayBounds_.h > 0;
    if (moved && settings_.keepRelativeWindowSize && screen_ == Screen::Video && !fullscreen_ && activeMode_) {
        int width = 0;
        int height = 0;
        if (SDL_GetWindowSize(window_, &width, &height) && width > 0 && height > 0) {
            const double ratio = std::min(static_cast<double>(bounds.w) / windowDisplayBounds_.w,
                                          static_cast<double>(bounds.h) / windowDisplayBounds_.h);
            int newWidth = std::max(160, static_cast<int>(std::lround(width * ratio)));
            int newHeight = std::max(
                90, static_cast<int>(std::lround(static_cast<double>(newWidth) * activeMode_->height / activeMode_->width)));
            if (newWidth > bounds.w || newHeight > bounds.h) {
                const double fit = std::min(static_cast<double>(bounds.w) / newWidth, static_cast<double>(bounds.h) / newHeight);
                newWidth = std::max(160, static_cast<int>(newWidth * fit));
                newHeight = std::max(90, static_cast<int>(newHeight * fit));
            }
            SDL_SetWindowAspectRatio(window_, 0.0f, 0.0f);
            SDL_SetWindowSize(window_, newWidth, newHeight);
            ApplyWindowConstraints();
            Log("[window] display changed: %dx%d -> %dx%d (relative size)", width, height, newWidth, newHeight);
        }
    }
    windowDisplay_ = display;
    windowDisplayBounds_ = bounds;
}

void App::OpenLastRecording() {
    std::error_code error;
    if (lastRecordingPath_.empty() || !std::filesystem::exists(lastRecordingPath_, error)) {
        const std::string message = T("The last recording file no longer exists.");
        if (screen_ == Screen::Settings) {
            errorMessage_ = message;
        } else {
            ShowToast(message, true);
        }
        return;
    }
    if (SDL_OpenURL(platform::FileUrl(lastRecordingPath_).c_str())) return;
    const std::string message = Format(T("Could not open the file: %s"), lastRecordingPath_.c_str());
    if (screen_ == Screen::Settings) {
        errorMessage_ = message;
    } else {
        ShowToast(message, true);
    }
}

SDL_HitTestResult SDLCALL App::AudioOnlyHitTest(SDL_Window* window, const SDL_Point* point, void* data) {
    auto* app = static_cast<App*>(data);
    if (!app || app->screen_ != Screen::AudioOnly) return SDL_HITTEST_NORMAL;
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (app->settings_.borderlessWindow) {
        constexpr int kEdge = 6;
        const bool left = point->x < kEdge;
        const bool right = point->x >= width - kEdge;
        const bool top = point->y < kEdge;
        const bool bottom = point->y >= height - kEdge;
        if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (top) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
    }
    if (app->menuVisible_ || app->helpVisible_ || app->confirmExit_ != ExitRequest::None) return SDL_HITTEST_NORMAL;
    const float density = std::max(SDL_GetWindowPixelDensity(window), 0.25f);
    const float x = static_cast<float>(point->x) * density;
    const float y = static_cast<float>(point->y) * density;
    const auto& controls = app->audioOnlyControls_;
    if (x >= controls[0] && x < controls[2] && y >= controls[1] && y < controls[3]) return SDL_HITTEST_NORMAL;
    return SDL_HITTEST_DRAGGABLE;
}

void App::NotifyFrame() {
    if (framePending_.exchange(true, std::memory_order_acq_rel)) return;
    SDL_Event event{};
    event.type = frameEvent_;
    if (!SDL_PushEvent(&event)) framePending_.store(false, std::memory_order_release);
}

bool App::IsWayland() const {
    return videoDriver_ == "wayland";
}

}
