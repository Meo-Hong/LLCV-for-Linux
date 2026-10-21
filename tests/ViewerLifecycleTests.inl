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

#ifdef LLCV_RECORDING
// Run the real recording worker with tiny CPU-encoded synthetic input. These
// cases exercise the actual viewer close protocol without modal interaction,
// physical capture devices, or writes outside the owned lifecycle directory.
static void TestRecordingViewerLifecycle(const wchar_t* windowClass,HINSTANCE instance,
                                        const std::wstring& directory) {
    using namespace llcv::recording;
    const bool savedSuppress=g_suppressSettingsSave;
    g_suppressSettingsSave=true;
    Require(RecordingCloseInteractive(L"LLCV.exe"),
            "profile isolation alone does not disable user close confirmation");
    Require(!RecordingCloseInteractive(L"LLCV.exe --smoke-test"),
            "explicit diagnostic run disables modal test interaction");
    Require(!RecordingCloseInteractive(),"lifecycle fixture must use --smoke-test");
    enum class Exit { StartupClose,ActiveClose,SettingsShortcut,CaptureEnded,Eos,Quit,SessionCancelled,SessionCritical };
    unsigned sequence=0;
    for(const Exit exit:{Exit::StartupClose,Exit::ActiveClose,Exit::SettingsShortcut,Exit::CaptureEnded,Exit::Eos,Exit::Quit,
                        Exit::SessionCancelled,Exit::SessionCritical}) {
        recorder.Shutdown();
        ViewerLifecycleFixture fixture;g_viewerLifecycleFixture=&fixture;
        g_settings={};g_settings.audioOnly=false;g_settings.preventSleep=false;
        g_fullscreen=false;g_autoFullscreen=false;g_videoHost=nullptr;
        g_running=true;g_restartToSettings=false;g_windowPositionPersisted=false;
        HWND hwnd=CreateWindowW(windowClass,L"Hidden recording-close fixture",WS_POPUP,
            -10000,-10000,400,240,nullptr,nullptr,instance,nullptr);
        Require(hwnd!=nullptr,"recording-close hidden viewer created");
        Require(BeginRecordingClose(hwnd,false) && !g_recordingClosePending,
                "idle close does not fabricate pending finalization");
        Require(SendMessageW(hwnd,WM_QUERYENDSESSION,0,0)==TRUE,
                "idle viewer does not block Windows shutdown");

        llcv::video::VideoSampleFormat video{};
        video.valid=true;video.width=160;video.height=90;video.fps=30;video.stride=160;
        video.format=llcv::settings::VideoPixelFormat::Nv12;
        llcv::capture_audio::Format audio{};
        audio.channels=2;audio.containerBits=audio.validBits=16;audio.blockAlign=4;
        audio.path=llcv::capture_audio::Path::Direct16BitStereo;
        Options options;options.encoder=L"cpu";options.rateControl=RateControl::Bitrate;
        options.videoMbps=2;
        const auto path=directory+L"\\recording-close-"+std::to_wstring(sequence++)+L".mp4";
        recorder.Configure(video,audio,{},false,options);
        Require(recorder.Start(path),"recording close fixture starts asynchronously");

        if(exit!=Exit::StartupClose) {
            const auto deadline=GetTickCount64()+5000;
            while(recorder.Busy() && !recorder.Recording() && GetTickCount64()<deadline)Sleep(2);
            Require(recorder.Recording(),"CPU recorder reaches active state before lifecycle test");
            Microsoft::WRL::ComPtr<IMemAllocator> allocator;
            Check(CoCreateInstance(CLSID_MemoryAllocator,nullptr,CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&allocator)),"recording-close sample allocator");
            constexpr LONG frameBytes=160*90*3/2;
            ALLOCATOR_PROPERTIES requested{2,frameBytes,1,0},actual{};
            Check(allocator->SetProperties(&requested,&actual),"recording-close allocator properties");
            Check(allocator->Commit(),"recording-close allocator commit");
            for(int frame=0;frame<4;++frame) {
                LONGLONG start=frame*10000000LL/30,end=(frame+1)*10000000LL/30;
                for(bool isVideo:{true,false}) {
                    Microsoft::WRL::ComPtr<IMediaSample> sample;
                    Check(allocator->GetBuffer(&sample,nullptr,nullptr,0),"recording-close synthetic sample");
                    const LONG bytes=isVideo?frameBytes:1600*4;
                    Check(sample->SetActualDataLength(bytes),"recording-close sample length");
                    Check(sample->SetTime(&start,&end),"recording-close sample time");
                    BYTE* data=nullptr;Check(sample->GetPointer(&data),"recording-close sample data");
                    std::memset(data,isVideo?128:0,bytes);
                    recorder.Push(isVideo,sample.Get());
                }
                Sleep(20);
            }
            Require(recorder.Recording(),"short recording remains active before close");
            if(exit==Exit::ActiveClose){
                WNDCLASSW settingsClass{};settingsClass.lpfnWndProc=SettingsWndProc;
                settingsClass.hInstance=instance;settingsClass.lpszClassName=L"LLCV.LiveRecordingSettings.Test";
                RegisterClassW(&settingsClass);
                const auto previousCapabilities=g_recordingCapabilities;
                g_recordingCapabilities=Capabilities{};
                (*g_recordingCapabilities)[0]={true,true,{{L"Fixture",L"test"}},{true,true},{true,true}};
                SettingsDialogState live;live.recordingLive=true;live.activeTab=SettingsTab::Recording;
                HWND dialog=CreateWindowW(settingsClass.lpszClassName,L"Live recording tab test",WS_OVERLAPPED,
                    -10000,-10000,1000,650,hwnd,nullptr,instance,&live);
                Require(dialog!=nullptr,"live recording tab creates without restarting playback");
                Require(!live.recordingProbe && !live.videoModeCache && !live.captureAudioProbeCache &&
                    !live.sharedProbeCache && !live.inventoryCache && !live.exclusiveScanRunning,
                    "live recording tab never rescans capture/audio devices or cached encoders");
                SendMessageW(dialog,WM_TIMER,2071,0);
                Require(llcv::settings_ui::CanToggleRecordingSettings(&live) && IsWindowEnabled(live.recOpenFolder) &&
                    !IsWindowEnabled(live.recEncoder),"live tab keeps recording shortcut and folder access, blocks option edits");
                SendMessageW(dialog,WM_CLOSE,0,0);
                Require(!IsWindow(dialog) && recorder.Recording() && IsWindow(hwnd),
                    "closing settings leaves recording and playback active");
                for(auto font:live.uiFonts)DeleteObject(font);
                g_recordingCapabilities=previousCapabilities;
                UnregisterClassW(settingsClass.lpszClassName,instance);
            }
        }

        if(exit==Exit::CaptureEnded) {
            const auto start=GetTickCount64();
            SendMessageW(hwnd,WM_RECORDING_CAPTURE_ENDED,0,0);
            Require(GetTickCount64()-start<1000 && IsWindow(hwnd) && g_running.load(),
                    "capture teardown requests finalization without joining the writer");
            Require(g_recordingDrainPending && !g_recordingClosePending && fixture.closed==0,
                    "capture teardown arms drain timeout without deciding viewer exit");
            const auto before=recorder.GetSnapshot().path;
            Require(ToggleRecording()==ToggleResult::Stopping && recorder.GetSnapshot().path==before,
                    "late shortcut cannot restart recording during capture drain");
            const auto deadline=GetTickCount64()+8000;
            while(recorder.Busy() && GetTickCount64()<deadline) {
                MSG message{};
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                    if(message.message==WM_QUIT)continue;
                    TranslateMessage(&message);DispatchMessageW(&message);
                }
                Sleep(2);
            }
            Require(!recorder.Busy(),"capture drain completes within deadline");
            SendMessageW(hwnd,WM_TIMER,1,0);
            Require(IsWindow(hwnd) && g_running.load() && fixture.closed==0,
                    "completed recording alone cannot destroy viewer before capture restart decision");
            // The real capture thread makes this decision only after Shutdown
            // returns. Its explicit restart intent must survive the final close.
            g_restartToSettings=true;g_running=false;
            PostMessageW(hwnd,WM_CLOSE,0,0);
            RunViewerMessageLoop(hwnd);
            Require(g_restartToSettings.load(),"capture restart intent survives delayed viewer destruction");
        } else if(exit==Exit::SessionCancelled) {
            Require(SendMessageW(hwnd,WM_QUERYENDSESSION,0,0)==FALSE,
                    "noncritical Windows shutdown defers while recording is busy");
            SendMessageW(hwnd,WM_ENDSESSION,FALSE,0);
            Require(IsWindow(hwnd) && g_running.load() && !g_recordingClosePending,
                    "cancelled Windows shutdown leaves viewer open");
            const auto deadline=GetTickCount64()+8000;
            while(recorder.Busy() && GetTickCount64()<deadline)Sleep(2);
            Require(!recorder.Busy() && PollRecordingClose(hwnd),
                    "cancelled shutdown still finishes recording without hanging");
            Require(!g_recordingShutdownBlocked && IsWindow(hwnd) && g_running.load(),
                    "completed file releases shutdown block without closing playback");
            DestroyWindow(hwnd);
        } else if(exit==Exit::SessionCritical) {
            Require(SendMessageW(hwnd,WM_QUERYENDSESSION,0,ENDSESSION_CRITICAL)==TRUE,
                    "critical system shutdown is not vetoed");
            SendMessageW(hwnd,WM_ENDSESSION,TRUE,0);
            Require(!g_running.load(),"confirmed session end leaves the normal viewer loop");
            RunViewerMessageLoop(hwnd);
        } else if(exit==Exit::Eos || exit==Exit::Quit) {
            if(exit==Exit::Eos) {
                g_running=false;
                PostMessageW(hwnd,WM_CLOSE,0,0);
            } else PostQuitMessage(0);
            RunViewerMessageLoop(hwnd);
        } else {
            const auto start=GetTickCount64();
            if(exit==Exit::SettingsShortcut) {
                g_requestedSettingsTab=static_cast<int>(SettingsTab::Recording);
                Require(HandleViewerShortcut(hwnd,VK_F2,0,0) && g_restartToSettings.load() &&
                    g_requestedSettingsTab==-1 && !g_recordingSettingsWindow,
                    "F2 during recording requests normal settings, not a live recording dialog");
                MSG close{};
                Require(PeekMessageW(&close,hwnd,WM_CLOSE,WM_CLOSE,PM_REMOVE)!=FALSE,
                    "recording settings shortcut queues the normal close protocol");
                DispatchMessageW(&close);
            } else SendMessageW(hwnd,WM_CLOSE,0,0);
            Require(GetTickCount64()-start<1000 && IsWindow(hwnd) && g_running.load(),
                    "close requests finalization without joining the recording worker");
            Require(g_recordingClosePending,"active/startup close retains pending state");
            if(exit==Exit::SettingsShortcut) {
                Require(HandleViewerShortcut(hwnd,VK_F2,0,0) && !g_recordingSettingsWindow,
                    "settings repeats cannot reopen a dialog during file finalization");
            }
            const auto before=recorder.GetSnapshot().path;
            Require(ToggleRecording()==ToggleResult::Stopping && recorder.GetSnapshot().path==before,
                    "late shortcut cannot start another recording while owner is closing");
            const auto deadline=GetTickCount64()+8000;
            while(IsWindow(hwnd) && GetTickCount64()<deadline) {
                MSG message{};
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                    if(message.message==WM_QUIT)continue;
                    TranslateMessage(&message);DispatchMessageW(&message);
                }
                if(IsWindow(hwnd) && !recorder.Busy())SendMessageW(hwnd,WM_TIMER,1,0);
                Sleep(2);
            }
            Require(!IsWindow(hwnd),"timer completes close after asynchronous file finalization");
        }
        Require(!recorder.Busy() && !IsWindow(hwnd) && fixture.destroyed==1,
                "every recording shutdown finalizes before destroying the viewer exactly once");
        if(exit==Exit::SettingsShortcut)Require(g_restartToSettings.load(),
            "recording file finalization preserves the requested normal-settings restart");
        if(exit!=Exit::StartupClose) {
            const auto saved=recorder.GetSnapshot();
            Require(saved.playable && saved.phase==Phase::Saved,
                    "recording survives normal EOS quit and session-end close paths");
        }
        recorder.Shutdown();
        Require(DeleteFileW(path.c_str())!=FALSE || GetLastError()==ERROR_FILE_NOT_FOUND,
                "remove only the recording created by the lifecycle fixture");
        MSG pending{};while(PeekMessageW(&pending,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
        g_videoHost=nullptr;
    }
    SetControlsClosing(false);
    g_suppressSettingsSave=savedSuppress;
    g_viewerLifecycleFixture=nullptr;
    std::puts("Recording viewer lifecycle: 8 asynchronous shutdown cases passed.");
}
#endif

static void TestMinimizedWindowPlacement() {
    const bool savedFullscreen=g_fullscreen, savedHaveWindowed=g_haveLastWindowedRect;
    const RECT savedWindowed=g_lastWindowedRect;
    g_fullscreen=false;
    // Synthetic monitor origins cover left/top taskbars and negative desktop
    // coordinates without changing the user's monitor or taskbar settings.
    for(POINT origin:{POINT{0,0},POINT{-1920,-1080},POINT{1920,0}})
    for(POINT inset:{POINT{0,0},POINT{48,0},POINT{0,40},POINT{48,40}}) {
        MONITORINFO monitor{sizeof(monitor)};
        monitor.rcMonitor={origin.x,origin.y,origin.x+1920,origin.y+1080};
        monitor.rcWork={origin.x+inset.x,origin.y+inset.y,origin.x+1920,origin.y+1040};
        const RECT normal{origin.x+100,origin.y+120,origin.x+500,origin.y+360};
        const RECT screen=NormalPlacementScreenRect(normal,0,monitor);
        Require(screen.left==normal.left+inset.x && screen.top==normal.top+inset.y &&
            screen.right-screen.left==400 && screen.bottom-screen.top==240,
            "ordinary placement converts workspace origin once while preserving extent");
        const RECT tool=NormalPlacementScreenRect(normal,WS_EX_TOOLWINDOW,monitor);
        Require(EqualRect(&normal,&tool),"tool-window placement is already in screen coordinates");
    }
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL {
        reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);return TRUE;
    },reinterpret_cast<LPARAM>(&monitors));
    Require(!monitors.empty(),"placement fixture enumerates display work areas");
    for(HMONITOR monitor:monitors) for(DWORD exStyle:{DWORD{0},DWORD{WS_EX_TOOLWINDOW}}) {
        MONITORINFOEXW info{};info.cbSize=sizeof(info);
        Require(GetMonitorInfoW(monitor,&info)!=FALSE,"placement fixture monitor information");
        HWND hwnd=CreateWindowExW(exStyle|WS_EX_NOACTIVATE,L"STATIC",L"Hidden placement regression",
            WS_POPUP|WS_MINIMIZEBOX,info.rcWork.left+60,info.rcWork.top+80,400,240,
            nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Require(hwnd!=nullptr,"placement fixture window");
        RECT expected{};Require(GetWindowRect(hwnd,&expected)!=FALSE,"normal fixture rectangle");
        ShowWindow(hwnd,SW_SHOWMINNOACTIVE);ShowWindow(hwnd,SW_HIDE);
        Require(IsIconic(hwnd)!=FALSE,"fixture remains iconic while hidden");
        PersistWindowPosition(hwnd);
        auto loaded=llcv::settings::LoadFromIni(SettingsPath()).settings;
        Require(loaded.hasWindowPosition && loaded.windowX==expected.left && loaded.windowY==expected.top &&
            loaded.monitorDevice==info.szDevice,
            "minimized close persists the normal rectangle and its original monitor, never -32000");
        g_fullscreen=true;g_haveLastWindowedRect=true;
        g_lastWindowedRect=expected;OffsetRect(&g_lastWindowedRect,10,15);
        PersistWindowPosition(hwnd);
        loaded=llcv::settings::LoadFromIni(SettingsPath()).settings;
        Require(loaded.windowX==expected.left+10 && loaded.windowY==expected.top+15,
            "fullscreen retains the saved windowed rectangle even when minimized");
        g_fullscreen=false;DestroyWindow(hwnd);
    }
    g_fullscreen=savedFullscreen;g_haveLastWindowedRect=savedHaveWindowed;
    g_lastWindowedRect=savedWindowed;
    std::puts("Window placement: workspace/tool-window conversion and minimized/fullscreen persistence passed.");
}

static void TestViewerLifecycle() {
    const auto savedSettings = g_settings;
    const bool savedSuppress = g_suppressSettingsSave;
#ifdef LLCV_RECORDING
    const int savedRequestedTab = g_requestedSettingsTab;
#endif
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
    TestMinimizedWindowPlacement();

    enum class Stop { BeforeLoop, DuringMessage, NormalClose, SettingsAction, ExplicitSettingsTab,
                      ExitShortcut, Quit, MissingClose };
    for (bool audioOnly : {false, true}) {
        for (Stop stop : {Stop::BeforeLoop, Stop::DuringMessage, Stop::NormalClose,
                          Stop::SettingsAction, Stop::ExplicitSettingsTab, Stop::ExitShortcut,
                          Stop::Quit, Stop::MissingClose}) {
            for (bool preventSleep : {false, true}) {
                ViewerLifecycleFixture fixture;
                fixture.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                fixture.posted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                Require(fixture.entered && fixture.posted, "lifecycle synchronization events");
                g_viewerLifecycleFixture = &fixture;
                g_settings = {};
                g_settings.audioOnly = audioOnly;
                g_settings.preventSleep = preventSleep;
                if (preventSleep) g_settings.shortcuts[static_cast<size_t>(keys::Action::Settings)]=keys::Ctrl|VK_F10;
                if (preventSleep) g_settings.shortcuts[static_cast<size_t>(keys::Action::Exit)]=keys::Ctrl|'Q';
                Require(keys::Valid(g_settings.shortcuts),"lifecycle fixture uses distinct shortcuts");
                g_settings.allowVolumeBoost = true;
                g_settings.volumePercent = 20;
                g_windowPositionPersisted = false;
                g_exclusiveStartupFallback = false;
                g_fullscreen = false;
                g_autoFullscreen = false;
                g_videoHost = nullptr;
                g_running.store(true);
                g_restartToSettings.store(false);
#ifdef LLCV_RECORDING
                g_requestedSettingsTab = static_cast<int>(SettingsTab::GuideDiagnostics);
#endif
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
                    if (stop==Stop::SettingsAction && preventSleep)
                        Require(!HandleViewerShortcut(hwnd,VK_F2,0,0),"old F2 does not open settings after remapping");
                    UINT message=shortcut?kLifecycleShortcutMessage:WM_CLOSE;
                    WPARAM argument=shortcut?g_settings.shortcuts[static_cast<size_t>(action)]:0;
                    if(stop==Stop::ExplicitSettingsTab){message=WM_OPEN_SETTINGS;argument=1;}
                    Require(PostMessageW(hwnd,message,argument,0) != FALSE,
                            "queue normal viewer shutdown");
                }
                RunViewerMessageLoop(hwnd);
                if (capture.joinable()) capture.join();
                Require(!IsWindow(hwnd) && fixture.destroyed == 1 && !g_running.load(),
                        "each loop exit destroys the viewer exactly once");
                const bool normal = stop == Stop::NormalClose || stop == Stop::SettingsAction ||
                    stop == Stop::ExplicitSettingsTab || stop == Stop::ExitShortcut;
                Require(fixture.closed == (normal ? 1u : 0u),
                        "fallback does not replay an already processed close");
                Require(!g_suspendResumeNotification.Registered(),
                        "all viewer exits unregister suspend/resume notifications");
                Require(g_restartToSettings.load() == (stop == Stop::SettingsAction || stop == Stop::ExplicitSettingsTab),
                        "settings restart intent is preserved");
#ifdef LLCV_RECORDING
                Require(g_requestedSettingsTab == (stop == Stop::SettingsAction ? -1 : static_cast<int>(SettingsTab::GuideDiagnostics)),
                        "settings key clears a stale page; explicit tab handoff preserves its target");
#endif
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
#ifdef LLCV_RECORDING
    TestRecordingViewerLifecycle(wc.lpszClassName,wc.hInstance,temporary);
#endif
    g_viewerLifecycleFixture = nullptr;
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    Require(DeleteFileW(SettingsPath().c_str()) != FALSE, "remove only owned lifecycle profile");
    Require(RemoveDirectoryW(temporary) != FALSE, "remove empty owned lifecycle directory");
    g_testUserDataDirectory.clear();
    g_settings = savedSettings;
    g_suppressSettingsSave = savedSuppress;
#ifdef LLCV_RECORDING
    g_requestedSettingsTab = savedRequestedTab;
#endif
    std::puts("Viewer lifecycle: 32 hidden-window shutdown/persistence cases passed.");
}
