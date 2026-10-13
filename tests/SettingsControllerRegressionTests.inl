// Included by VsrExperimentTests: hidden HWNDs and injected workers only.
static void TestSettingsShortcutStatusRefresh() {
    const auto saved=g_settings;
    const bool savedSuppress=g_suppressSettingsSave;
    g_suppressSettingsSave=true;
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpfnWndProc=SettingsControllerTestProc; wc.lpszClassName=L"LLCV_SHORTCUT_STATUS_TEST";
    Require(RegisterClassW(&wc)!=0,"register shortcut status fixture");
    SettingsDialogState state; state.asioAvailable=true;
    HWND hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"Shortcut status fixture",
        WS_POPUP|WS_VISIBLE,-32000,-32000,800,600,nullptr,nullptr,wc.hInstance,&state);
    Require(hwnd!=nullptr,"shortcut status owner");
    const auto child=[&](const wchar_t* name,DWORD style) {
        HWND value=CreateWindowW(name,L"",WS_CHILD|WS_VISIBLE|style,0,0,350,40,hwnd,nullptr,wc.hInstance,nullptr);
        Require(value!=nullptr,"shortcut status control"); return value;
    };
    state.audioCombo=child(L"COMBOBOX",CBS_DROPDOWNLIST);
    for(const auto* mode:{L"Shared",L"Exclusive",L"ASIO"}) SendMessageW(state.audioCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(mode));
    SendMessageW(state.audioCombo,CB_SETCURSEL,0,0);
    state.surround51Check=child(L"BUTTON",BS_AUTOCHECKBOX);
    SendMessageW(state.surround51Check,BM_SETCHECK,BST_CHECKED,0);
    state.audioStatus=child(L"STATIC",SS_LEFT);
    state.guideVideoHint=child(L"STATIC",SS_LEFT);
    for(auto& key:state.guideKeys) key=child(L"BUTTON",BS_PUSHBUTTON);
    const auto text=[&] { wchar_t value[512]{}; GetWindowTextW(state.audioStatus,value,512); return std::wstring(value); };
    const auto edit=[&](UINT key) {
        llcv::settings_ui::BeginShortcutEdit(&state,static_cast<size_t>(keys::Action::Diagnostics));
        MSG input{}; input.hwnd=GetFocus(); input.message=WM_KEYDOWN; input.wParam=key;
        Require(ProcessSettingsShortcutEdit(&state,input,keys::Ctrl),"controller consumes shortcut assignment");
        input.message=WM_KEYUP;
        Require(ProcessSettingsShortcutEdit(&state,input,0),"controller consumes assignment key release");
    };
    UpdateAudioClient3Status(&state);
    Require(text().find(L"Tab")!=std::wstring::npos,"5.1 starts with default diagnostic key");
    edit('D');
    Require(text().find(L"Ctrl+D")!=std::wstring::npos && text().find(L"Tab")==std::wstring::npos,
        "5.1 diagnostic hint updates immediately on assignment");
    SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_SETTINGS_SHORTCUT_RESET,BN_CLICKED),0);
    Require(state.shortcutBindings==keys::Defaults && text().find(L"Tab")!=std::wstring::npos &&
        text().find(L"Ctrl+D")==std::wstring::npos,"reset updates both binding and 5.1 hint");
    for(int mode:{1,2,0}) {
        SendMessageW(state.audioCombo,CB_SETCURSEL,mode,0);
        SendMessageW(state.surround51Check,BM_SETCHECK,BST_UNCHECKED,0);
        SetWindowTextW(state.audioStatus,L"Exclusive result must survive");
        UpdateAudioClient3Status(&state);
        const auto previous=text();
        edit('K');
        Require(text()==previous,"shortcut update preserves non-5.1 and Exclusive status");
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_SETTINGS_SHORTCUT_RESET,BN_CLICKED),0);
        Require(text()==previous,"shortcut reset preserves non-5.1 and Exclusive status");
    }
    Require(!state.sharedProbeCache && !state.exclusiveScanRunning,"editing never starts hardware probes");
    DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName,wc.hInstance);
    g_settings=saved; g_suppressSettingsSave=savedSuppress;
    std::puts("Shortcut status: immediate edit/reset refresh and preserved audio modes passed.");
}

static void TestSettingsBufferModeTransitions() {
    const auto saved = g_settings;
    const bool savedSuppress = g_suppressSettingsSave;
    const bool savedFallback = g_exclusiveStartupFallback;
    g_suppressSettingsSave = true;
    g_settings.audioMode = AudioMode::WasapiExclusive;
    g_settings.audioOnly = true;
    g_settings.wasapiBufferMs = 20;
    g_settings.wasapiSharedPeriodFrames = 960;
    g_settings.audioOutputDeviceId = L"buffer-fixture";
    g_settings.asioDriverName = L"Buffer fixture ASIO";
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = SettingsControllerTestProc;
    wc.lpszClassName = L"LLCV_SETTINGS_BUFFER_TEST";
    Require(RegisterClassW(&wc) != 0, "register buffer fixture");
    SettingsDialogState state;
    state.activeTab = SettingsTab::Audio;
    state.asioAvailable = true;
    state.asioDrivers = {{0, "Buffer fixture ASIO"}};
    state.audioEndpoints = {{L"buffer-fixture", L"Buffer fixture", true}};
    state.exclusiveEndpointResults.resize(1);
    state.exclusiveEndpointResults[0].state = ExclusiveEndpointState::Supported;
    state.exclusiveEndpointResults[0].recommendedBufferMs = 5;
    state.exclusiveScanCompleted = 1;
    state.probeReady = true;
    state.appliedSharedEndpoint = L"buffer-fixture";
    state.probe = {true, S_OK, 480, 48, 96, 1920, 0.0};
    state.selectedBufferMs = 20;
    state.selectedSharedPeriodFrames = 960;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Hidden buffer fixture", WS_POPUP,
        -32000, -32000, 1000, 650, nullptr, nullptr, wc.hInstance, &state);
    Require(hwnd != nullptr, "create buffer fixture");
    const llcv::settings_ui::SettingsControlInitialValues initial{
        g_settings, false, true, kAppVersionLabel, VideoPreset::R1920x1080,
        {}, {}, kVideoPresets, kPcmQueueOptionsMs, {}};
    const llcv::settings_ui::SettingsControlPopulation population{
        &state, [](void* s) { PopulateAudioOutputCombo(static_cast<SettingsDialogState*>(s)); },
        [](void* s) { PopulateSettingsBufferCombo(static_cast<SettingsDialogState*>(s)); },
        [](void*) {}};
    llcv::settings_ui::CreateSettingsDialogControls(&state, hwnd, wc.hInstance, initial, population);
    auto mode = [&](int selection) {
        SendMessageW(state.audioCombo, CB_SETCURSEL, selection, 0);
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SETTINGS_AUDIO, CBN_SELCHANGE), 0);
    };
    auto displayedBuffer = [&] {
        const LRESULT row = SendMessageW(state.bufferCombo, CB_GETCURSEL, 0, 0);
        return SendMessageW(state.bufferCombo, CB_GETITEMDATA, row, 0);
    };
    auto chooseBuffer = [&](int value) {
        const LRESULT count = SendMessageW(state.bufferCombo, CB_GETCOUNT, 0, 0);
        for (LRESULT row = 0; row < count; ++row) {
            if (SendMessageW(state.bufferCombo, CB_GETITEMDATA, row, 0) != value) continue;
            SendMessageW(state.bufferCombo, CB_SETCURSEL, row, 0);
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SETTINGS_BUFFER, CBN_SELCHANGE), 0);
            return;
        }
        Require(false, "requested buffer exists in fixture");
    };
    // Both legacy Shared milliseconds and IAudioClient3 frame periods must
    // survive the ASIO explanatory row, without a device query or rescan.
    for (bool sharedLowLatency : {false, true}) {
        state.probe.supported = sharedLowLatency;
        for (int milliseconds : kExclusiveBufferOptionsMs) {
            mode(1);
            chooseBuffer(milliseconds);
            mode(2);
            Require(!IsWindowEnabled(state.bufferCombo), "ASIO buffer is informational only");
            RememberCurrentBufferChoice(&state); // Also used when accepting ASIO.
            Require(state.selectedBufferMs == milliseconds && state.selectedSharedPeriodFrames == 960,
                "ASIO placeholder must not overwrite either saved WASAPI buffer");
            mode(1);
            Require(state.selectedBufferMs == milliseconds && displayedBuffer() == milliseconds &&
                    IsWindowEnabled(state.startButton),
                "Exclusive buffer and enabled Start survive an ASIO round trip");
            mode(0);
            const LRESULT sharedChoice = sharedLowLatency ? 960 : milliseconds;
            Require(displayedBuffer() == sharedChoice, "Shared uses its own buffer units");
            mode(2);
            mode(0);
            Require(displayedBuffer() == sharedChoice && state.selectedBufferMs == milliseconds &&
                    state.selectedSharedPeriodFrames == 960 && IsWindowEnabled(state.startButton),
                "Shared buffer survives ASIO with and without IAudioClient3");
        }
    }
    mode(1);
    chooseBuffer(30);
    state.exclusiveEndpointResults[0].recommendedBufferMs = 10;
    mode(2);
    mode(1);
    Require(displayedBuffer() == 30 && HasExclusiveVerificationForSelection(&state) &&
            IsWindowEnabled(state.startButton),
        "returning from ASIO retains a buffer above the verified Exclusive minimum");
    mode(2);
    Require(!state.exclusiveScanRunning && !state.sharedProbeCache && !state.captureAudioProbeCache,
        "buffer-only mode changes never start hardware probes");
    SendMessageW(state.preventSleepCheck, BM_SETCHECK, BST_UNCHECKED, 0);
    FinishSettingsDialog(hwnd, &state, true);
    Require(state.accepted && !IsWindow(hwnd) && g_settings.audioMode == AudioMode::Asio &&
            g_settings.wasapiBufferMs == 30 && g_settings.wasapiSharedPeriodFrames == 960,
        "accepting ASIO preserves both WASAPI preferences without saving a zero buffer");
    Require(!g_settings.preventSleep, "accepting settings applies the sleep prevention checkbox");
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_settings = saved;
    g_suppressSettingsSave = savedSuppress;
    g_exclusiveStartupFallback = savedFallback;
    std::puts("Settings buffers: all 6 Exclusive values, both Shared APIs, ASIO round trips and acceptance passed.");
}

static void TestSettingsSharedRefreshAcrossModes() {
    const auto saved = g_settings;
    const bool savedSuppress = g_suppressSettingsSave;
    g_suppressSettingsSave = true;
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = SettingsControllerTestProc;
    wc.lpszClassName = L"LLCV_SETTINGS_SHARED_REFRESH_TEST";
    Require(RegisterClassW(&wc) != 0, "register Shared refresh fixture");
    struct Case { int mode; bool automatic; bool changeDefault; };
    for (const auto test : {Case{2, false, false}, Case{2, true, false},
                            Case{2, true, true}, Case{0, false, false}, Case{1, false, false}}) {
        g_settings = {};
        g_settings.audioOnly = true;
        g_settings.audioOutputDeviceId = L"A"; // The dialog may have an unaccepted choice of B.
        g_settings.asioDriverName = L"Refresh fixture ASIO";
        SettingsDialogState state;
        state.activeTab = SettingsTab::Audio;
        state.asioAvailable = true;
        state.asioDrivers = {{0, "Refresh fixture ASIO"}};
        state.audioEndpoints = {{L"A", L"Output A", true}, {L"B", L"Output B", false}};
        state.exclusiveEndpointResults.resize(2);
        for (auto& result : state.exclusiveEndpointResults) {
            result.state = ExclusiveEndpointState::Supported;
            result.recommendedBufferMs = 10;
        }
        state.exclusiveScanCompleted = 2;
        state.selectedBufferMs = 20;
        state.selectedSharedPeriodFrames = 960;
        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Hidden Shared refresh fixture", WS_POPUP,
            -32000, -32000, 1000, 650, nullptr, nullptr, wc.hInstance, &state);
        Require(hwnd != nullptr, "create Shared refresh fixture");
        const llcv::settings_ui::SettingsControlInitialValues initial{
            g_settings, false, true, kAppVersionLabel, VideoPreset::R1920x1080,
            {}, {}, kVideoPresets, kPcmQueueOptionsMs, {}};
        const llcv::settings_ui::SettingsControlPopulation population{
            &state, [](void* s) { PopulateAudioOutputCombo(static_cast<SettingsDialogState*>(s)); },
            [](void* s) { PopulateSettingsBufferCombo(static_cast<SettingsDialogState*>(s)); },
            [](void*) {}};
        llcv::settings_ui::CreateSettingsDialogControls(&state, hwnd, wc.hInstance, initial, population);
        std::array<std::atomic<unsigned>, 2> queries{};
        std::atomic<unsigned> minimum{48}, inventoryQueries{0};
        state.sharedProbeCache = std::make_unique<llcv::settings_ui::SettingsModeCache<AudioClient3Support>>(
            [&](const auto& key) {
                Require(key.device == L"A" || key.device == L"B", "Shared query uses endpoint identity");
                ++queries[key.device == L"B" ? 1 : 0];
                return AudioClient3Support{true, S_OK, 480, 48, minimum.load(), 1920, 0.0};
            }, [hwnd] { PostMessageW(hwnd, WM_AUDIOCLIENT3_PROBE_COMPLETE, 0, 0); });
        state.inventoryCache = std::make_unique<llcv::settings_ui::SettingsModeCache<SettingsDeviceInventory>>(
            [&, test](const auto&) {
                ++inventoryQueries;
                SettingsDeviceInventory inventory;
                // Reordering must not turn a saved endpoint identity into a list index.
                inventory.audioOutputs = {{L"B", L"Output B", test.changeDefault},
                                         {L"A", L"Output A", !test.changeDefault}};
                return inventory;
            }, [hwnd] { PostMessageW(hwnd, WM_SETTINGS_INVENTORY_READY, 0, 0); });
        state.videoModeCache = std::make_unique<llcv::settings_ui::SettingsModeCache<SettingsVideoModes>>(
            [](const auto&) { return SettingsVideoModes{}; },
            [hwnd] { PostMessageW(hwnd, WM_SETTINGS_VIDEO_MODES_READY, 0, 0); });
        state.captureAudioProbeCache = std::make_unique<llcv::settings_ui::SettingsModeCache<InternalCaptureAudioProbe>>(
            [](const auto&) { return InternalCaptureAudioProbe{}; },
            [hwnd] { PostMessageW(hwnd, WM_CAPTURE_AUDIO_PROBE_COMPLETE, 0, 0); });
        const auto pump = [&](auto done) {
            const auto deadline = GetTickCount64() + 3000;
            while (!done() && GetTickCount64() < deadline) {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
                MSG message{};
                while (PeekMessageW(&message, hwnd, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
            }
            Require(done(), "injected refresh work completes without hardware");
        };
        const auto output = [&](int row) {
            SendMessageW(state.audioOutputCombo, CB_SETCURSEL, row, 0);
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SETTINGS_AUDIO_OUTPUT, CBN_SELCHANGE), 0);
            pump([&] { return state.probeReady; });
        };
        const auto mode = [&](int selection) {
            SendMessageW(state.audioCombo, CB_SETCURSEL, selection, 0);
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SETTINGS_AUDIO, CBN_SELCHANGE), 0);
        };
        output(1); output(2); // Cache both endpoints before refreshing only the selected one.
        output(test.automatic ? 0 : 2);
        mode(test.mode);
        minimum = 96;
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SETTINGS_VIDEO_REFRESH, BN_CLICKED), 0);
        pump([&] { return !state.inventoryPending; });
        if (test.mode != 0) Require(queries[0] == 1 && queries[1] == 1,
            "refresh outside Shared only invalidates; it does not eagerly probe audio");
        mode(0);
        pump([&] { return state.probeReady; });
        const size_t target = !test.automatic || test.changeDefault ? 1 : 0;
        Require(state.probe.minimumFrames == 96 && state.appliedSharedEndpoint == (target ? L"B" : L"A"),
            "Shared return uses refreshed capabilities of the preserved selection/current default");
        Require(!state.sharedPeriodChoices.empty() && state.sharedPeriodChoices.front() == 96 &&
                state.selectedSharedPeriodFrames == 960 && state.selectedBufferMs == 20,
            "refreshed buffer choices preserve the user's still-valid buffer preferences");
        Require(SelectedAudioEndpointId(&state) == (test.automatic ? L"" : L"B") &&
                g_settings.audioOutputDeviceId == L"A",
            "refresh preserves unaccepted selection and never changes saved preferences");
        for (unsigned repeat = 0; repeat < 20; ++repeat) { mode(2); mode(1); mode(0); }
        Require(queries[target] == 2 && queries[1-target] == 1 && inventoryQueries == 1,
            "one refresh queries only its target once; subsequent mode changes remain cached");
        Require(!state.exclusiveScanRunning && !state.exclusiveProbeThread.joinable() &&
                state.exclusiveScanCompleted == 2 && IsWindowEnabled(state.startButton),
            "Shared refresh preserves Exclusive verdicts and start eligibility without rescanning");
        state.sharedProbeCache.reset(); state.inventoryCache.reset();
        state.videoModeCache.reset(); state.captureAudioProbeCache.reset();
        DestroyWindow(hwnd);
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_settings = saved;
    g_suppressSettingsSave = savedSuppress;
    std::puts("Shared refresh: ASIO explicit/default/default-change, Shared/Exclusive controls, preserved buffers and bounded query counts passed.");
}

static void TestSettingsAudioIdentityAndRefresh() {
    const auto saved = g_settings;
    g_settings = {};
    g_settings.audioOutputDeviceId = L"B";
    g_settings.asioDriverName = L"Driver one";
    WNDCLASSW wc{};
    wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=SettingsControllerTestProc;
    wc.lpszClassName=L"LLCV_SETTINGS_IDENTITY_TEST";
    Require(RegisterClassW(&wc)!=0,"register identity fixture");
    SettingsDialogState state;
    state.activeTab=SettingsTab::Audio;
    state.asioAvailable=true;
    state.asioDrivers={{0,"Driver one"},{1,"Driver two"}};
    for (const auto* id : {L"A",L"B",L"C"}) {
        AudioEndpointInfo endpoint{}; endpoint.id=id; endpoint.name=id; endpoint.isDefault=id==std::wstring(L"A");
        state.audioEndpoints.push_back(endpoint);
    }
    state.exclusiveEndpointResults.resize(3);
    for (auto& result:state.exclusiveEndpointResults) result.state=ExclusiveEndpointState::Supported;
    state.exclusiveScanCompleted=3;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"Hidden identity fixture",WS_POPUP,
        -32000,-32000,1000,650,nullptr,nullptr,wc.hInstance,&state);
    Require(hwnd!=nullptr,"create identity fixture");
    const llcv::settings_ui::SettingsControlInitialValues initial{
        g_settings,false,true,kAppVersionLabel,VideoPreset::R1920x1080,
        {},{},kVideoPresets,kPcmQueueOptionsMs,{}};
    const llcv::settings_ui::SettingsControlPopulation population{
        &state,[](void* s){PopulateAudioOutputCombo(static_cast<SettingsDialogState*>(s));},
        [](void* s){PopulateSettingsBufferCombo(static_cast<SettingsDialogState*>(s));},[](void*){}};
    llcv::settings_ui::CreateSettingsDialogControls(&state,hwnd,wc.hInstance,initial,population);
    std::atomic<unsigned> queries{0};
    HANDLE started=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    state.sharedProbeCache=std::make_unique<llcv::settings_ui::SettingsModeCache<AudioClient3Support>>(
        [&](const auto& key) {
            ++queries;
            if (key.device==L"B") {
                SetEvent(started);
                Require(WaitForSingleObject(release,3000)==WAIT_OBJECT_0,"release slow fake shared driver");
            }
            AudioClient3Support result{}; result.minimumFrames=key.device==L"B"?48:96;
            return result;
        },[hwnd]{PostMessageW(hwnd,WM_AUDIOCLIENT3_PROBE_COMPLETE,0,0);});
    auto mode=[&](int value) {
        SendMessageW(state.audioCombo,CB_SETCURSEL,value,0);
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_SETTINGS_AUDIO,CBN_SELCHANGE),0);
    };
    RequestSettingsSharedProbe(&state,hwnd);
    Require(WaitForSingleObject(started,2000)==WAIT_OBJECT_0,"fake query starts off UI thread");
    const auto before=GetTickCount64();
    SendMessageW(state.audioOutputCombo,CB_SETCURSEL,3,0);
    SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_SETTINGS_AUDIO_OUTPUT,CBN_SELCHANGE),0);
    mode(2);
    Require(SelectedAsioDriverName(&state)==L"Driver one","WASAPI index never selects unrelated ASIO driver");
    SendMessageW(state.audioOutputCombo,CB_SETCURSEL,1,0);
    mode(0);
    Require(SelectedAudioEndpointId(&state)==L"C","return from ASIO preserves actual WASAPI endpoint ID");
    Require(GetTickCount64()-before<500,"mode/output switches do not join slow driver query");
    SetEvent(release);
    const auto deadline=GetTickCount64()+3000;
    while ((!state.probeReady || state.appliedSharedEndpoint!=L"C") && GetTickCount64()<deadline) {
        MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);
        MSG message{};
        while(PeekMessageW(&message,hwnd,0,0,PM_REMOVE)) DispatchMessageW(&message);
    }
    Require(state.probeReady && state.probe.minimumFrames==96 && queries==2,
        "late result B cannot overwrite selected C and pending requests coalesce");
    for(int i=0;i<30;++i) {
        mode(2); Require(SelectedAsioDriverName(&state)==L"Driver two","ASIO selection retained independently");
        mode(1); Require(SelectedAudioEndpointId(&state)==L"C","Exclusive shares WASAPI identity");
        mode(0);
    }
    Require(queries==2 && !state.exclusiveScanRunning,"cached mode changes never repeat probes");

    state.captureDevices={{L"capture-B",L"Card B"},{L"capture-A",L"Card A"}};
    state.captureAudioDevices={{L"mic-B",L"Mic B"}};
    llcv::settings_ui::PopulateCaptureDevicePicker(state.captureDeviceCombo,state.captureDevices,L"capture-B",false,false);
    llcv::settings_ui::PopulateCaptureDevicePicker(state.captureAudioDeviceCombo,state.captureAudioDevices,L"mic-B",true,false);
    SettingsDeviceInventory changed;
    changed.videoInputs={{L"capture-A",L"Card A"},{L"capture-B",L"Card B"},{L"capture-new",L"New card"}};
    changed.audioInputs={{L"mic-new",L"New mic"},{L"mic-B",L"Mic B"}};
    changed.audioOutputs={state.audioEndpoints[2],state.audioEndpoints[0],state.audioEndpoints[1]};
    state.exclusiveEndpointResults[2].recommendedBufferMs=17;
    ApplySettingsInventory(&state,changed);
    Require(SelectedCaptureDeviceId(&state)==L"capture-B" && SelectedCaptureAudioDeviceId(&state)==L"mic-B" &&
        SelectedAudioEndpointId(&state)==L"C","inventory reorder preserves all selected IDs");
    Require(state.captureDevices.size()==3 && state.captureAudioDevices.size()==2 &&
        state.exclusiveEndpointResults[0].recommendedBufferMs==17 && state.exclusiveScanCompleted==3,
        "refresh discovers attached devices and retains Exclusive verdicts by endpoint identity");
    changed.videoInputs={{L"capture-A",L"Card A"}};
    changed.audioOutputs={state.audioEndpoints[1]};
    ApplySettingsInventory(&state,changed);
    Require(SelectedCaptureDeviceId(&state)==L"capture-B" && SelectedAudioEndpointId(&state)==L"C",
        "disconnection never silently replaces explicit selections with default");
    state.sharedProbeCache.reset();
    CloseHandle(started); CloseHandle(release);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName,wc.hInstance);
    g_settings=saved;
    std::puts("Settings identity: 3 endpoints, 2 ASIO drivers, slow/stale probe, cache reuse and inventory reorder passed.");
}
