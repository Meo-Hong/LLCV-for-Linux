// Included by VsrExperimentTests. Hidden HWNDs, no capture/audio devices.
// The private build redirects all profile writes to one owned temporary folder.
struct ViewerLifecycleFixture {
    HANDLE entered = nullptr, posted = nullptr;
    unsigned closed = 0, destroyed = 0;
};
static ViewerLifecycleFixture* g_viewerLifecycleFixture = nullptr;
static constexpr UINT kLifecycleEarlierMessage = WM_APP + 0x123;
static constexpr UINT kLifecycleShortcutMessage = WM_APP + 0x124;

static LRESULT CALLBACK ViewerLifecycleTestProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto& fixture = *g_viewerLifecycleFixture;
    if (message==kLifecycleShortcutMessage) {
        Require(HandleViewerShortcut(hwnd,w & 0xff,0,static_cast<unsigned>(w) & 0x700),
                "configured shortcut reaches actual viewer handler");
        Require(HandleViewerShortcut(hwnd,w & 0xff,LPARAM{1}<<30,static_cast<unsigned>(w) & 0x700),
                "held settings or exit key is consumed without repeating shutdown");
        return 0;
    }
    if (message == kLifecycleEarlierMessage) {
        SetEvent(fixture.entered);
        Require(WaitForSingleObject(fixture.posted, 5000) == WAIT_OBJECT_0,
                "capture completion overtakes earlier UI message");
        return 0;
    }
    if (message == WM_CLOSE) ++fixture.closed;
    if (message == WM_DESTROY) ++fixture.destroyed;
    return WndProc(hwnd, message, w, l);
}

static void TestViewerLifecycle() {
    const auto savedSettings = g_settings;
    const bool savedSuppress = g_suppressSettingsSave;
    wchar_t directory[MAX_PATH]{}, temporary[MAX_PATH]{};
    Require(GetTempPathW(ARRAYSIZE(directory), directory) != 0 &&
            GetTempFileNameW(directory, L"lvc", 0, temporary) != 0,
            "reserve isolated lifecycle directory");
    Require(DeleteFileW(temporary) && CreateDirectoryW(temporary, nullptr),
            "create isolated lifecycle directory");
    g_testUserDataDirectory = temporary;
    Require(SettingsPath() == std::wstring(temporary) + L"\\settings.ini",
            "lifecycle tests never touch the user's profile");
    g_suppressSettingsSave = false;
    Require(g_playbackPowerEvents.Initialize(true) == ERROR_SUCCESS, "lifecycle wake initialized");
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = ViewerLifecycleTestProc;
    wc.lpszClassName = L"LLCV_VIEWER_LIFECYCLE_TEST";
    Require(RegisterClassW(&wc) != 0, "register lifecycle fixture");

    enum class Stop { BeforeLoop, DuringMessage, NormalClose, SettingsAction, ExitShortcut, Quit, MissingClose };
    for (bool audioOnly : {false, true}) {
        for (Stop stop : {Stop::BeforeLoop, Stop::DuringMessage, Stop::NormalClose,
                          Stop::SettingsAction, Stop::ExitShortcut, Stop::Quit, Stop::MissingClose}) {
            for (bool preventSleep : {false, true}) {
                ViewerLifecycleFixture fixture;
                fixture.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                fixture.posted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                Require(fixture.entered && fixture.posted, "lifecycle synchronization events");
                g_viewerLifecycleFixture = &fixture;
                g_settings = {};
                g_settings.audioOnly = audioOnly;
                g_settings.preventSleep = preventSleep;
                if (preventSleep) g_settings.shortcuts[static_cast<size_t>(keys::Action::Settings)]=VK_F9;
                if (preventSleep) g_settings.shortcuts[static_cast<size_t>(keys::Action::Exit)]=keys::Ctrl|'Q';
                g_settings.allowVolumeBoost = true;
                g_settings.volumePercent = 20;
                g_windowPositionPersisted = false;
                g_exclusiveStartupFallback = false;
                g_fullscreen = false;
                g_autoFullscreen = false;
                g_videoHost = nullptr;
                g_running.store(true);
                g_restartToSettings.store(false);
                HWND hwnd = CreateWindowW(wc.lpszClassName, L"Hidden lifecycle fixture",
                    WS_POPUP, 80, 90, 400, 240, nullptr, nullptr, wc.hInstance, nullptr);
                Require(hwnd != nullptr, "create hidden viewer with real lifecycle handlers");
                Require(g_suspendResumeNotification.Registered() == preventSleep,
                        "viewer subscribes only when sleep prevention is enabled");
                RECT expected{};
                Require(GetWindowRect(hwnd, &expected) != FALSE, "read hidden viewer placement");
                g_volumePercent.store(135);
                g_leftVolumePercent.store(75);
                g_rightVolumePercent.store(65);
                std::thread capture;
                if (stop == Stop::DuringMessage || stop == Stop::MissingClose) {
                    Require(PostMessageW(hwnd, kLifecycleEarlierMessage, 0, 0) != FALSE,
                            "queue earlier UI message");
                    capture = std::thread([&] {
                        Require(WaitForSingleObject(fixture.entered, 5000) == WAIT_OBJECT_0,
                                "UI entered message before capture stops");
                        g_running.store(false);
                        if (stop == Stop::DuringMessage)
                            Require(PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE, "queue EOS close");
                        SetEvent(fixture.posted);
                    });
                } else if (stop == Stop::BeforeLoop) {
                    g_running.store(false);
                    Require(PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE, "queue pre-loop EOS close");
                } else if (stop == Stop::Quit) {
                    PostQuitMessage(0);
                } else {
                    // Inject the complete synthetic key event into the same
                    // handler used by both real window/message-loop routes.
                    const bool shortcut=stop==Stop::SettingsAction || stop==Stop::ExitShortcut;
                    const auto action=stop==Stop::SettingsAction ? keys::Action::Settings : keys::Action::Exit;
                    if (stop==Stop::ExitShortcut && preventSleep)
                        Require(!HandleViewerShortcut(hwnd,VK_ESCAPE,0,0),"old Esc does not close after remapping");
                    Require(PostMessageW(hwnd,shortcut ? kLifecycleShortcutMessage : WM_CLOSE,
                        shortcut ? g_settings.shortcuts[static_cast<size_t>(action)] : 0,0) != FALSE,
                            "queue normal viewer shutdown");
                }
                RunViewerMessageLoop(hwnd);
                if (capture.joinable()) capture.join();
                Require(!IsWindow(hwnd) && fixture.destroyed == 1 && !g_running.load(),
                        "each loop exit destroys the viewer exactly once");
                const bool normal = stop == Stop::NormalClose || stop == Stop::SettingsAction || stop == Stop::ExitShortcut;
                Require(fixture.closed == (normal ? 1u : 0u),
                        "fallback does not replay an already processed close");
                Require(!g_suspendResumeNotification.Registered(),
                        "all viewer exits unregister suspend/resume notifications");
                Require(g_restartToSettings.load() == (stop == Stop::SettingsAction),
                        "settings restart intent is preserved");
                const auto loaded = llcv::settings::LoadFromIni(SettingsPath()).settings;
                Require(loaded.volumePercent == 135 && loaded.leftVolumePercent == 75 &&
                        loaded.rightVolumePercent == 65,
                        "all loop exits persist the final master and channel volumes");
                Require(loaded.hasWindowPosition && loaded.windowX == expected.left &&
                        loaded.windowY == expected.top,
                        "all loop exits persist the final window placement");
                Require(loaded.preventSleep == preventSleep && loaded.audioOnly == audioOnly,
                        "shutdown preserves playback mode and sleep preference");
                Require(loaded.shortcuts==g_settings.shortcuts,"shutdown retains custom shortcuts");
                // Fallback WM_DESTROY posts WM_QUIT after the loop has returned.
                MSG pending{};
                while (PeekMessageW(&pending, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
                CloseHandle(fixture.entered);
                CloseHandle(fixture.posted);
                g_videoHost = nullptr;
            }
        }
    }
    g_viewerLifecycleFixture = nullptr;
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    Require(DeleteFileW(SettingsPath().c_str()) != FALSE, "remove only owned lifecycle profile");
    Require(RemoveDirectoryW(temporary) != FALSE, "remove empty owned lifecycle directory");
    g_testUserDataDirectory.clear();
    g_settings = savedSettings;
    g_suppressSettingsSave = savedSuppress;
    std::puts("Viewer lifecycle: 28 hidden-window shutdown/persistence cases passed.");
}
