#include "app/App.h"

#include "app/AppInfo.h"
#include "app/ModeSelection.h"
#include "capture/UvcDescriptors.h"
#include "diagnostics/Logger.h"
#include "platform/Clock.h"
#include "platform/Paths.h"
#include "platform/PngImage.h"
#include "platform/Strings.h"
#include "settings/SettingsStore.h"
#include "ui/Text.h"
#include "video/GlApi.h"

#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstring>

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
    ApplyLanguage();
    if (!InitializeWindow()) return 1;
    InitializeImGui();
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
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
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

bool App::CreateGlWindow(int colorBits, std::string& error) {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
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
        error = std::string("OpenGL 3.3 context: ") + SDL_GetError();
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
    if (deepColor) {
        created = CreateGlWindow(10, error);
        if (!created) {
            Log("[gl] 10-bit framebuffer unavailable (%s); using 8-bit", error.c_str());
            DestroyGlWindow();
            error.clear();
        }
    }
    if (!created) created = CreateGlWindow(8, error);
    if (created) {
        std::string missing;
        if (!video::LoadGlApi(missing)) error = "OpenGL function missing: " + missing;
    }
    if (error.empty() && !renderer_.Initialize(error)) error = "renderer: " + error;
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
    ImGui_ImplOpenGL3_Init("#version 330 core");
    fonts_ = ui::LoadFonts();
    imguiInitialized_ = true;
    ApplyStyle();
}

void App::Shutdown() {
    capture_.Stop();
    audio_.Stop();
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
    if (imguiInitialized_) platform_.ProcessEvent(event);
    redrawFrames_ = std::max(redrawFrames_, 2);

    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        running_ = false;
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
        break;
    default:
        break;
    }
}

void App::HandleKey(const SDL_KeyboardEvent& key) {
    if (key.repeat || screen_ == Screen::Settings) return;
    const bool alt = (key.mod & SDL_KMOD_ALT) != 0;
    switch (key.key) {
    case SDLK_F1:
        helpVisible_ = !helpVisible_;
        break;
    case SDLK_F2:
        StopSession();
        EnterSettings();
        break;
    case SDLK_F3:
        if (screen_ == Screen::Video) audioOsdVisible_ = !audioOsdVisible_;
        break;
    case SDLK_F5:
        if (screen_ == Screen::Video) {
            if (fullscreen_) SetFullscreen(false);
            ApplyVideoWindowSize(false);
        }
        break;
    case SDLK_F11:
        SetFullscreen(!fullscreen_);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (alt) SetFullscreen(!fullscreen_);
        break;
    case SDLK_F12:
        if (screen_ == Screen::Video) RequestScreenshot();
        break;
    case SDLK_TAB:
        diagnosticsVisible_ = !diagnosticsVisible_;
        if (diagnosticsVisible_) audio_.ResetMinimum();
        break;
    case SDLK_ESCAPE:
        if (menuVisible_) {
            menuVisible_ = false;
        } else if (helpVisible_) {
            helpVisible_ = false;
        } else if (fullscreen_) {
            SetFullscreen(false);
        } else {
            running_ = false;
        }
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
    audioError_ = T("오디오 장치 연결이 끊겼습니다 · 다시 연결되면 자동으로 복구합니다",
                    "Audio device disconnected · it is restored automatically when it returns");
    ShowToast(audioError_, true);
    audioRetryMs_ = platform::MonotonicMs() + 1000;
}

void App::Update() {
    const uint64_t now = platform::MonotonicMs();
    PollScreenshots();

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
            ShowToast(T("오디오가 복구되었습니다.", "Audio restored."));
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
    if (animating || (liveOverlay && now - lastRenderMs_ >= 100)) redrawFrames_ = std::max(redrawFrames_, 1);
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

void App::UpdateCursor(uint64_t now) {
    const bool autoHide = screen_ == Screen::Video && fullscreen_ &&
                          settings_.fullscreenCursor == settings::FullscreenCursor::AutoHide && !helpVisible_ &&
                          !audioOsdVisible_ && !menuVisible_;
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
    switch (settingsScreen_.Draw(settings_, context)) {
    case ui::SettingsAction::Start:
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
    case ui::SettingsAction::None:
        break;
    }
}

void App::DrawVideoOverlays() {
    if (capture_.State() == capture::CaptureState::Reconnecting) {
        ui::DrawCenterNotice(T("캡처 장치 연결 끊김", "Capture device disconnected"),
                             T("장치가 다시 연결되면 자동으로 재생합니다.",
                               "Playback resumes automatically when the device reconnects."),
                             fonts_, scale_);
    } else if (noSignalShown_) {
        ui::DrawCenterNotice(T("신호 대기 중", "Waiting for signal"),
                             T("HDMI 입력 기기와 캡처 장치 상태를 확인하세요. HDR 출력은 꺼야 합니다.",
                               "Check the HDMI source and the capture device. HDR output must be off."),
                             fonts_, scale_);
    }
    if (audioOsdVisible_) {
        ui::AudioMeterState state = BuildAudioState();
        if (ui::DrawAudioOsd(state, fonts_, scale_)) ApplyAudioState(state);
    }
    DrawCommonOverlays();
}

void App::DrawAudioOnly() {
    ui::AudioMeterState state = BuildAudioState();
    if (ui::DrawAudioOnlyView(state, fonts_, scale_)) ApplyAudioState(state);
    DrawCommonOverlays();
}

void App::DrawCommonOverlays() {
    const uint64_t now = platform::MonotonicMs();
    if (diagnosticsVisible_) ui::DrawDiagnostics(BuildDiagnostics(), fonts_, scale_);
    const float hudAlpha = FadeAlpha(now, volumeHudUntilMs_);
    if (hudAlpha > 0.0f) {
        ui::DrawVolumeHud(settings_.volumePercent, settings_.muteWhenBackground && !focused_,
                          settings_.volumeHudPosition, hudAlpha, scale_);
    }
    const float toastAlpha = FadeAlpha(now, toastUntilMs_);
    if (toastAlpha > 0.0f) ui::DrawToast(toastText_, toastError_, toastAlpha, scale_);
    if (helpVisible_) ui::DrawHelp(&helpVisible_, screen_ == Screen::AudioOnly, SessionHdrSupport(), fonts_, scale_);
    if (menuVisible_) DrawMenu();
}

std::vector<ui::DiagnosticsLine> App::BuildDiagnostics() const {
    std::vector<ui::DiagnosticsLine> lines;
    const bool english = ui::IsEnglish();
    lines.push_back({T("경로", "Path"), Format("V4L2 · OpenGL · %s%s", videoDriver_.c_str(),
                                              fullscreen_ ? T(" · 전체화면", " · fullscreen") : "")});
    if (screen_ == Screen::Video && activeMode_) {
        const auto negotiated = capture_.Negotiated();
        const auto& color = negotiated.color;
        lines.push_back({T("장치", "Device"), activeDeviceName_});
        lines.push_back({T("입력", "Input"),
                         Format("%d × %d @ %s fps · %s · %s %s (%s)", negotiated.width, negotiated.height,
                                capture::FormatRate(negotiated.rate).c_str(),
                                capture::PixelFormatName(negotiated.format), video::MatrixName(color.matrix),
                                video::RangeName(color.range),
                                video::SourceName(color.matrixSource, color.rangeSource, english))});
        const auto rect = renderer_.LastVideoRect();
        const double ratio = negotiated.width ? static_cast<double>(rect.width) / negotiated.width : 0.0;
        lines.push_back({T("표시", "Display"),
                         Format("%d × %d · %.2fx · %s · %s", rect.width, rect.height, ratio,
                                settings_.presentationMode == settings::PresentationMode::Immediate
                                    ? T("저지연", "Immediate")
                                    : "VSync",
                                settings_.scalingMode == settings::ScalingMode::Sharp ? T("선명하게", "Sharp")
                                                                                       : T("부드럽게", "Smooth"))});
        lines.push_back({T("실제 FPS", "Actual FPS"),
                         Format(T("입력 %.1f · 표시 %.1f", "Input %.1f · Present %.1f"), inputFps_, presentFps_)});
        lines.push_back({T("앱 처리 지연", "App latency"),
                         Format(T("수신→표시 %s · 복사/디코드 %s (총 HDMI 지연 아님)",
                                  "capture→present %s · copy/decode %s (not total HDMI latency)"),
                                Ms(averageLatencyMs_).c_str(), Ms(averageProcessingMs_).c_str())});
        const auto& counters = capture_.Counters();
        lines.push_back({T("프레임", "Frames"),
                         Format(T("입력 %llu · 표시 %llu · 최신화 건너뜀 %llu", "Input %llu · Presented %llu · Replaced %llu"),
                                U(counters.frames.load()), U(presentedFrames_),
                                U(counters.replaced.load() + counters.skippedInQueue.load()))});
        std::string hdrText;
        if (color.transfer == video::Transfer::Pq) {
            hdrText = hdrActive_
                          ? Format(T("입력 HDR10 · HDR10 패스스루 (%d비트 버퍼)", "HDR10 input · HDR10 passthrough (%d-bit buffer)"),
                                   framebufferBits_)
                          : Format(T("입력 HDR10 · SDR 톤매핑 (기준 흰색 %d nit)", "HDR10 input · SDR tone mapping (%d nit white)"),
                                   settings_.sdrWhiteNits);
        } else {
            hdrText = T("SDR 입력", "SDR input");
        }
        const ui::HdrSupport support = SessionHdrSupport();
        if (color.transfer == video::Transfer::Pq && !hdrActive_ && !support.output) {
            hdrText += T(" · HDR 출력 미지원", " · HDR output not supported");
        } else if (color.transfer != video::Transfer::Pq && !support.input) {
            hdrText += T(" · HDR 입력 미지원", " · HDR input not supported");
        }
        if (preferredColor_.known) {
            hdrText += preferredColor_.hdr ? T(" · 모니터 HDR", " · display HDR") : T(" · 모니터 SDR", " · display SDR");
        }
        lines.push_back({"HDR", hdrText});
        lines.push_back({T("프레임 손실", "Frame loss"),
                         Format(T("드라이버 누락 %llu · 손상 %llu · 버퍼 %d개", "Driver dropped %llu · corrupt %llu · %d buffers"),
                                U(counters.driverDropped.load()), U(counters.corrupt.load()), negotiated.bufferCount)});
    }

    const audio::AudioStatus status = audio_.Status();
    if (status.running) {
        lines.push_back({T("오디오 출력", "Audio output"),
                         Format("SDL3 %s · %s", status.driver.c_str(), status.outputName.c_str())});
        lines.push_back({T("캡처 오디오", "Capture audio"),
                         Format(T("%s · 패킷 %.2f ms", "%s · packet %.2f ms"), status.captureName.c_str(),
                                FramesToMs(status.lastCapturePacketFrames))});
        lines.push_back({T("장치 버퍼", "Device buffer"),
                         Format(T("출력 %.2f ms · 입력 %.2f ms", "output %.2f ms · input %.2f ms"),
                                FramesToMs(static_cast<uint64_t>(std::max(status.outputDeviceFrames, 0))),
                                FramesToMs(static_cast<uint64_t>(std::max(status.captureDeviceFrames, 0))))});
        lines.push_back({T("앱 PCM 버퍼", "App PCM queue"),
                         Format(T("현재 %.2f ms · 목표 %.2f ms · 관측 최저 %.2f ms%s",
                                  "current %.2f ms · target %.2f ms · observed min %.2f ms%s"),
                                FramesToMs(status.ringFrames + status.resamplerFrames), FramesToMs(status.targetFrames),
                                FramesToMs(status.minimumQueueFrames),
                                status.prefilling ? T(" · 버퍼링", " · buffering") : "")});
        lines.push_back({T("클록 보정", "Clock drift"),
                         status.driftCorrection
                             ? Format(T("자동 · 적용 %+d ppm", "Auto · applied %+d ppm"), status.appliedPpm)
                             : std::string(T("끔 · 원본 PCM", "Off · unaltered PCM"))});
        lines.push_back({T("오디오 오류", "Audio errors"),
                         Format(T("underrun %llu회 (누락 %.2f ms) · overrun %llu회 · 지연 트림 %llu회",
                                  "underrun %llu (missing %.2f ms) · overrun %llu · latency trims %llu"),
                                U(status.underrunEvents), FramesToMs(status.underrunFrames), U(status.overrunEvents),
                                U(status.trimEvents))});
    } else {
        std::string text = audioWanted_ ? (audioError_.empty() ? std::string(T("시작 대기", "Waiting")) : audioError_)
                                        : std::string(T("사용 안 함", "Disabled"));
        lines.push_back({T("오디오", "Audio"), text});
    }
    lines.push_back({T("음량", "Volume"),
                     Format("%d%% · L %d%% · R %d%%%s", settings_.volumePercent, settings_.leftVolumePercent,
                            settings_.rightVolumePercent,
                            settings_.muteWhenBackground && !focused_ ? T(" · 백그라운드 음소거", " · muted in background")
                                                                      : "")});
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
        state.outputName = settings_.audioOutputDevice.empty() ? T("시스템 기본 출력", "System default output")
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
    context.logDirectory = logDirectoryText_;
    context.screenshotDirectory = screenshotDirectoryText_;
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
                                   ? Format(T("오디오 전용 · %s", "Audio only · %s"), audioName.c_str())
                                   : std::string(T("캡처 오디오 장치를 선택하세요.", "Select a capture audio device."));
    } else if (!device) {
        context.startSummary = T("캡처 장치가 없습니다.", "No capture device.");
    } else if (const auto mode = capture::ResolveMode(*device, RequestFromSettings(settings_))) {
        context.canStart = true;
        context.startSummary =
            Format("%s · %d × %d · %s · %s fps · %s", device->Name().c_str(), mode->width, mode->height,
                   capture::PixelFormatName(mode->format), capture::FormatRate(mode->rate).c_str(),
                   audioName.empty() ? T("오디오 없음", "no audio") : T("오디오 사용", "with audio"));
    } else {
        context.startSummary = T("선택한 모드를 장치가 지원하지 않습니다.", "The device does not support the selected mode.");
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
            error = T("캡처 장치를 찾지 못했습니다. USB 연결을 확인하세요.",
                      "No capture device was found. Check the USB connection.");
            return false;
        }
        activeMode_ = capture::ResolveMode(*device, RequestFromSettings(settings_));
        if (!activeMode_) {
            if (settings_.pixelFormat == settings::PixelFormatChoice::P010 &&
                !device->Supports(capture::PixelFormat::P010)) {
                error = Format(T("HDR 입력 미지원: %s", "HDR input not supported: %s"),
                               EvaluateHdrSupport(device).inputText.c_str());
            } else {
                error = T("선택한 해상도·형식을 장치가 지원하지 않습니다.",
                          "The device does not support the selected resolution and format.");
            }
            return false;
        }
    }
    captureAudioName_ = ResolveCaptureAudio(device);
    audioWanted_ = !captureAudioName_.empty();
    if (settings_.audioOnly && !audioWanted_) {
        error = T("캡처 오디오 장치를 찾지 못했습니다. 오디오 → 캡처 오디오 장치를 선택하세요.",
                  "No capture audio device was found. Select one under Audio → Capture audio device.");
        return false;
    }

    if (activeMode_) {
        capture::CaptureConfig config;
        config.devicePath = device->id;
        config.format = activeMode_->format;
        config.width = activeMode_->width;
        config.height = activeMode_->height;
        config.rate = activeMode_->rate;
        config.colorOverride = settings_.colorOverride;
        config.hdrInput = settings_.hdrInput;
        std::string captureError;
        if (!capture_.Start(config, mailbox_, [this] { NotifyFrame(); }, captureError)) {
            error = std::string(T("캡처를 시작하지 못했습니다: ", "Could not start capture: ")) + captureError;
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
                error = std::string(T("오디오를 시작하지 못했습니다: ", "Could not start audio: ")) + audioError;
                return false;
            }
            audioError_ = audioError;
            audioRetryMs_ = platform::MonotonicMs() + 2000;
            ShowToast(std::string(T("오디오를 시작하지 못했습니다: ", "Could not start audio: ")) + audioError, true);
        }
    } else if (!settings_.audioOnly && settings_.captureAudioDevice != settings::kCaptureAudioDisabled) {
        ShowToast(T("캡처 오디오 장치를 찾지 못해 영상만 재생합니다.", "No capture audio device was found; playing video only."),
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
            ShowToast(Format(T("HDR 출력 미지원: %s · SDR로 변환해 표시합니다.", "HDR output not supported: %s · showing as SDR."),
                             support.outputText.c_str()),
                      true);
        }
    } else if (toastUntilMs_ < platform::MonotonicMs()) {
        ShowToast(T("Alt 키를 누르면 메뉴가 나타납니다.", "Press Alt to show the menu."));
    }
    UpdateBackgroundMute();
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
    if (fullscreen_) SetFullscreen(false);
    SDL_SetWindowAspectRatio(window_, 0.0f, 0.0f);
    SDL_SetWindowBordered(window_, true);
    SDL_SetWindowResizable(window_, true);
    SDL_SetWindowMinimumSize(window_, 760, 520);
    SDL_SetWindowSize(window_, kSettingsWidth, kSettingsHeight);
    SDL_SetWindowTitle(window_, T("LLCV 설정", "LLCV Settings"));
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
    return audio_.Start(config, error);
}

void App::ConfigureSessionWindow() {
    SDL_SetWindowTitle(window_, kAppName);
    if (screen_ == Screen::AudioOnly) {
        SDL_SetWindowAspectRatio(window_, 0.0f, 0.0f);
        SDL_SetWindowBordered(window_, true);
        SDL_SetWindowResizable(window_, true);
        SDL_SetWindowMinimumSize(window_, 320, 220);
        SDL_SetWindowSize(window_, settings_.audioOnlyWidth, settings_.audioOnlyHeight);
        SDL_ShowWindow(window_);
        SDL_RaiseWindow(window_);
        return;
    }
    SDL_SetWindowMinimumSize(window_, 160, 90);
    SDL_SetWindowBordered(window_, !settings_.borderlessWindow);
    SDL_ShowWindow(window_);
    SDL_SyncWindow(window_);
    PositionOnPreferredDisplay();
    ApplyVideoWindowSize(true);
    SDL_RaiseWindow(window_);
    if (settings_.startFullscreen) SetFullscreen(true);
}

void App::ApplyVideoWindowSize(bool useSavedSize) {
    if (!activeMode_ || !window_) return;
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
        support.inputText = T("캡처 장치가 없습니다.", "No capture device.");
    } else if (device->Supports(capture::PixelFormat::P010)) {
        support.input = true;
        support.inputText = T("P010 (10비트 HDR10) 사용 가능 · 픽셀 형식에서 P010을 선택하세요.",
                              "P010 (10-bit HDR10) is available · select P010 as the pixel format.");
    } else if (device->advertisesP010) {
        support.inputText = Format(T("장치는 P010(HDR10)을 제공하지만 커널 %s의 uvcvideo가 인식하지 못합니다. "
                                     "리눅스 7.1 이상 또는 P010 DKMS 모듈이 필요합니다.",
                                     "The device offers P010 (HDR10), but uvcvideo in kernel %s does not recognize it. "
                                     "Linux 7.1 or newer, or a P010 DKMS module, is required."),
                                   kernelRelease_.c_str());
    } else {
        support.inputText = T("장치가 10비트 HDR 형식을 제공하지 않습니다. 8비트 HDR 신호는 'HDR10 강제'로 해석할 수 있습니다.",
                              "The device offers no 10-bit HDR format. 8-bit HDR signals can use 'Force HDR10'.");
    }

    if (forceSdr_) {
        support.outputText = T("--sdr 옵션으로 실행 중이라 HDR 출력을 끕니다.", "HDR output is off because LLCV was started with --sdr.");
    } else if (!video::HdrOutput::Compiled()) {
        support.outputText = T("이 빌드에는 Wayland 색 관리가 없습니다.", "This build lacks Wayland color management.");
    } else if (!IsWayland()) {
        support.outputText = T("X11에서는 HDR 출력을 할 수 없습니다. Wayland로 실행하세요.",
                               "HDR output is unavailable on X11. Run on Wayland.");
    } else if (!hdrOutput_.Connected()) {
        support.outputText = T("컴포지터가 Wayland 색 관리(wp_color_management_v1)를 지원하지 않습니다. "
                               "GNOME 48 이상 또는 KDE Plasma 6이 필요합니다.",
                               "The compositor lacks Wayland color management (wp_color_management_v1). "
                               "GNOME 48 or newer, or KDE Plasma 6, is required.");
    } else if (!hdrOutput_.Supported()) {
        support.outputText = T("컴포지터가 PQ/BT.2020 HDR10을 지원하지 않습니다.",
                               "The compositor does not support PQ/BT.2020 HDR10.");
    } else if (framebufferBits_ < 10) {
        support.outputText = T("그래픽 드라이버가 10비트 화면 버퍼를 제공하지 않습니다. "
                               "'항상 HDR 출력'을 선택하면 8비트로 출력합니다.",
                               "The graphics driver provides no 10-bit framebuffer. "
                               "'Always HDR output' sends 8-bit HDR instead.");
    } else if (preferredColor_.known && !preferredColor_.hdr) {
        support.output = true;
        support.outputText = T("지원 · 현재 모니터의 HDR이 꺼져 있습니다. 디스플레이 설정에서 HDR을 켜세요.",
                               "Supported · HDR is off on this display. Turn it on in the display settings.");
    } else {
        support.output = true;
        support.outputText = Format(T("지원 · %d비트 화면 버퍼 · Wayland 색 관리", "Supported · %d-bit framebuffer · Wayland color management"),
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
        if (!hdrActive_) ShowToast(T("HDR 출력을 켜지 못해 SDR 톤매핑으로 표시합니다.", "HDR output failed; using SDR tone mapping."), true);
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
    case settings::UiLanguage::Korean: ui::SetEnglish(false); break;
    case settings::UiLanguage::English: ui::SetEnglish(true); break;
    case settings::UiLanguage::Auto: ui::SetEnglish(!ui::SystemPrefersKorean()); break;
    }
    if (window_ && screen_ == Screen::Settings) SDL_SetWindowTitle(window_, T("LLCV 설정", "LLCV Settings"));
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
    volumeHudUntilMs_ = platform::MonotonicMs() + kVolumeHudMs;
}

void App::RememberWindowGeometry() {
    if (fullscreen_ || !window_) return;
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
        ShowToast(T("스크린샷 실패 · 표시 중인 영상이 없습니다.", "Screenshot failed · no video frame yet."), true);
        return;
    }
    screenshots_.Request(std::move(rgba), width, height, platform::ScreenshotDirectory(),
                         settings_.screenshotClipboard);
    ShowToast(T("스크린샷 저장 중…", "Saving screenshot…"));
}

void App::PollScreenshots() {
    while (auto result = screenshots_.Poll()) {
        if (!result->ok) {
            Log("[screenshot] failed: %s", result->error.c_str());
            ShowToast(Format(T("스크린샷 실패: %s", "Screenshot failed: %s"), result->error.c_str()), true);
            continue;
        }
        Log("[screenshot] saved %s", result->path.c_str());
        std::string message = Format(T("스크린샷 저장: %s", "Screenshot saved: %s"), result->path.filename().c_str());
        if (renderer_.SourceIsPq()) message += T(" (HDR→SDR 변환)", " (HDR converted to SDR)");
        if (!result->png.empty()) {
            CopyPngToClipboard(std::move(result->png));
            message += T(" · 클립보드 복사됨", " · copied to clipboard");
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
    const std::string message = Format(T("폴더를 열지 못했습니다: %s", "Could not open the folder: %s"), path.c_str());
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
    state.volume = settings_.volumePercent;
    const ui::MenuCommand command = ui::DrawMenuBar(state, scale_);
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
        if (screen_ == Screen::Video) {
            if (fullscreen_) SetFullscreen(false);
            ApplyVideoWindowSize(false);
        }
        break;
    case ui::MenuCommand::Borderless:
        settings_.borderlessWindow = !settings_.borderlessWindow;
        if (screen_ == Screen::Video) SDL_SetWindowBordered(window_, !settings_.borderlessWindow);
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
        StopSession();
        EnterSettings();
        break;
    case ui::MenuCommand::Help:
        helpVisible_ = true;
        break;
    case ui::MenuCommand::Quit:
        running_ = false;
        break;
    case ui::MenuCommand::None:
        break;
    }
}

void App::SaveSettings() {
    if (!settings::SaveSettings(settingsPath_, settings_)) Log("[settings] could not save %s", settingsPath_.c_str());
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
