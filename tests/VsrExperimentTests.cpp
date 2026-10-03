// Production renderer, synthetic NV12, real GPU. No capture/audio devices,
// visible windows, NVIDIA profile writes, user settings loads or saves.
#include "../src/main.cpp"
#undef fwprintf
#include "../src/audio/AsioOutput.cpp"
#include <limits>

static void Require(bool value, const char* label) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
static void Check(HRESULT hr, const char* label) {
    if (FAILED(hr)) { std::fprintf(stderr, "FAIL: %s 0x%08X\n", label, static_cast<unsigned>(hr)); std::exit(1); }
}
static void TestPolicy() {
    using namespace llcv::vsr;
    Require(Eligible(0x10de, DXGI_FORMAT_NV12, false, 1280,720,1920,1080), "720p to 1080p eligible");
    const auto savedSettings = g_settings;
    g_settings.videoPreset = VideoPreset::R1280x720;
    g_settings.videoFrameRate = 0;
    Require(CurrentVideoPreset().width == 1280 && CurrentVideoPreset().height == 720 &&
            RequestedVideoFrameRate() == 60, "720p capture preset defaults to 60 fps");
    g_settings.videoFrameRate = 120;
    Require(RequestedVideoFrameRate() == 120, "720p explicit FPS retained");
    g_settings.videoPreset = static_cast<VideoPreset>(-1);
    Require(CurrentVideoPreset().preset == VideoPreset::R1920x1080,
            "invalid preset fallback remains 1080p");
    // Resolve the capture/display plan once, before a graph starts. F6 must
    // never turn a 720p graph into a 4K graph or change window dimensions.
    for (const auto& display : kVideoPresets) for (const auto& capture : kVideoPresets) {
        g_settings.videoPreset = display.preset;
        g_settings.vsrCapturePreset = capture.preset;
        g_settings.vsrEnabled = true;
        g_settings.audioOnly = false;
        g_settings.pixelPerfect = true;
        g_settings.videoFrameRate = 0;
        LatchVideoResolutionPlan();
        Require(CurrentCapturePreset().preset == capture.preset, "VSR capture selection");
        const SIZE initial = InitialClientPixelsForMonitor(nullptr);
        Require(initial.cx == display.width && initial.cy == display.height, "explicit VSR display selection");
        Require(RequestedVideoFrameRate() == capture.framerate, "FPS default follows capture");
        Require(SourcePixelPerfect() == (display.preset == capture.preset), "split display never letterboxes as 1:1");
        g_vsrMode.store(Mode::On);
        for (int toggle = 0; toggle < 20; ++toggle) {
            HandleVsrTestKey(VK_F6, 0);
            Require(CurrentCapturePreset().preset == capture.preset &&
                InitialClientPixelsForMonitor(nullptr).cx == display.width,
                "F6 preserves latched capture and display across repeated toggles");
        }
    }
    g_resolutionPlanLatched = false;
    g_vsrResolutionPlan = false;
    g_vsrMode.store(Mode::Disabled);
    g_settings = savedSettings;
    Require(Eligible(0x10de, DXGI_FORMAT_NV12, false, 1920,1080,3840,2160), "NV12 upscale eligible");
    Require(!Eligible(0x1002, DXGI_FORMAT_NV12, false, 1920,1080,3840,2160), "AMD untouched");
    Require(!Eligible(0x8086, DXGI_FORMAT_NV12, false, 1920,1080,3840,2160), "Intel untouched");
    Require(!Eligible(0x10de, DXGI_FORMAT_P010, true, 1920,1080,3840,2160), "HDR untouched");
    Require(!Eligible(0x10de, DXGI_FORMAT_YUY2, false, 1920,1080,3840,2160), "YUY2 deferred");
    Require(Eligible(0x10de, DXGI_FORMAT_NV12, false, 1280,720,1280,720), "720p native eligible");
    Require(Eligible(0x10de, DXGI_FORMAT_NV12, false, 1920,1080,1920,1080), "1080p native eligible");
    Require(!Eligible(0x10de, DXGI_FORMAT_NV12, false, 1920,1080,1280,720), "downscale bypass");
    Require(Eligible(0x10de, DXGI_FORMAT_NV12, false, 1920,1080,3840,1080), "one unchanged axis eligible");
    Require(Eligible(0x10de, DXGI_FORMAT_NV12, false, 1920,1080,1920,2160), "other unchanged axis eligible");
    Require(!Eligible(0x10de, DXGI_FORMAT_NV12, false, 1920,1080,1919,2160), "width downscale bypass");
    Require(!Eligible(0x10de, DXGI_FORMAT_NV12, false, 1920,1080,3840,1079), "height downscale bypass");
    Require(!Eligible(0x10de, DXGI_FORMAT_NV12, false, 0,1080,3840,2160), "invalid size bypass");
    Require(!Eligible(0x10de, DXGI_FORMAT_NV12, false, 1920,0,3840,2160), "zero height bypass");
    Require(!Eligible(0x10de, DXGI_FORMAT_NV12, false, 1920,1080,0,1080), "zero output width bypass");
    Require(!Eligible(0x10de, DXGI_FORMAT_NV12, false, 1920,1080,1920,0), "zero output height bypass");
    Require(!Eligible(0x10de, DXGI_FORMAT_NV12, true, 1920,1080,1920,1080), "native HDR bypass");
    Require(!Eligible(0x10de, DXGI_FORMAT_P010, false, 1920,1080,1920,1080), "native P010 bypass");
    Require(!Eligible(0x1002, DXGI_FORMAT_NV12, false, 1920,1080,1920,1080), "native AMD bypass");
    Require(SetRequest(nullptr, nullptr, true) == E_POINTER, "null API guard");
    Distribution d;
    d.Add(-1); d.Add(std::numeric_limits<double>::infinity());
    d.Add(std::numeric_limits<double>::quiet_NaN());
    Require(!d.count && d.Percentile(.99) == 0, "empty/invalid measurements");
    for (int i = 0; i < 100; ++i) d.Add(1);
    d.Add(150);
    Require(d.count == 101 && d.maximum == 150 && d.Percentile(.99) < 1.2 &&
        d.Percentile(1) == 150, "histogram percentile and overflow");
    Timing timing;
    timing.Begin(nullptr); timing.End(nullptr, false); timing.AddCaptureToPresent(2);
    Require(!timing.Snapshot().captureToPresent.count, "disabled timing inert");
}

static LRESULT CALLBACK SettingsControllerTestProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // Exercise the real controller and keyboard mappings without WM_CREATE's
    // hardware enumeration or background workers. The fixture supplies controls.
    if (message == WM_CREATE) return 0;
    return SettingsWndProc(hwnd,message,wParam,lParam);
}
static void TestSettingsController() {
    const auto savedSettings = g_settings;
    const bool savedSuppress = g_suppressSettingsSave;
    const HWND previousForeground = GetForegroundWindow();
    g_suppressSettingsSave = true;
    g_settings.captureDeviceId = L"controller-test-sentinel";
    g_settings.videoPreset = VideoPreset::R2560x1440;
    g_settings.audioOnly = false;
    g_settings.vsrEnabled = true;
    g_settings.volumePercent = 73;
    g_settings.pcmQueueTargetMs = 25;
    auto unchanged = [&] {
        Require(g_settings.captureDeviceId == L"controller-test-sentinel" &&
            g_settings.videoPreset == VideoPreset::R2560x1440 && !g_settings.audioOnly &&
            g_settings.vsrEnabled && g_settings.volumePercent == 73 && g_settings.pcmQueueTargetMs == 25,
            "blocked/cancelled controller action preserves settings");
    };
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = SettingsControllerTestProc;
    wc.lpszClassName = L"LLCV_SETTINGS_CONTROLLER_TEST";
    Require(RegisterClassW(&wc) != 0, "controller test class");
    auto create = [&](SettingsDialogState& state) {
        state.activeTab = SettingsTab::VideoWindow;
        // Offscreen native controls permit real focus/dropdown behavior. No
        // capture/audio device, settings file, update request or worker opens.
        HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT | WS_EX_NOACTIVATE,
            wc.lpszClassName,L"Offscreen settings controller",WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
            -32000,-32000,950,650,nullptr,nullptr,wc.hInstance,&state);
        Require(hwnd != nullptr, "controller fixture window");
        auto child = [&](const wchar_t* kind, const wchar_t* text, DWORD style, int id) {
            HWND result = CreateWindowW(kind,text,WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                10,10,300,120,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);
            Require(result != nullptr, "controller fixture child");
            return result;
        };
        state.tabControl = child(L"LISTBOX",L"",LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED,IDC_SETTINGS_TAB);
        for (const auto* label : {L"Video",L"Audio",L"Window",L"Guide",L"App"})
            SendMessageW(state.tabControl,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        state.audioCombo = child(L"COMBOBOX",L"",CBS_DROPDOWNLIST,IDC_SETTINGS_AUDIO);
        for (const auto* label : {L"Shared",L"Exclusive"})
            SendMessageW(state.audioCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(state.audioCombo,CB_SETCURSEL,0,0);
        state.audioOnlyCheck = child(L"BUTTON",L"Audio only",BS_AUTOCHECKBOX,IDC_SETTINGS_AUDIO_ONLY);
        state.pixelFormatCombo = child(L"COMBOBOX",L"",CBS_DROPDOWNLIST,IDC_SETTINGS_PIXEL_FORMAT);
        const auto nv12 = SendMessageW(state.pixelFormatCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"NV12"));
        SendMessageW(state.pixelFormatCombo,CB_SETITEMDATA,nv12,static_cast<LPARAM>(VideoPixelFormat::Nv12));
        SendMessageW(state.pixelFormatCombo,CB_SETCURSEL,nv12,0);
        state.frameRateCombo = child(L"COMBOBOX",L"",CBS_DROPDOWNLIST,IDC_SETTINGS_FRAME_RATE);
        SendMessageW(state.frameRateCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"60 fps"));
        state.startButton = child(L"BUTTON",L"Start",BS_DEFPUSHBUTTON,IDC_SETTINGS_START);
        state.cancelButton = child(L"BUTTON",L"Cancel",BS_PUSHBUTTON,IDC_SETTINGS_CANCEL);
        Require(GetDlgCtrlID(state.startButton) == 2004 && GetDlgItem(hwnd,IDOK) == nullptr,
            "fixture retains production non-IDOK Start identifier");
        LayoutSettingsControls(&state,96);
        UpdateAdvancedControlVisibility(&state);
        Require(state.theme.Attach(hwnd,&state), "controller fixture theme");
        return hwnd;
    };
    auto key = [](HWND owner, HWND target, WPARAM value) {
        SetFocus(target);
        Require(GetFocus() == target, "controller keyboard fixture focus");
        MSG message{}; message.hwnd = target; message.message = WM_KEYDOWN;
        message.wParam = value; message.lParam = 1;
        if (!IsDialogMessageW(owner,&message)) DispatchMessageW(&message);
    };
    {
        SettingsDialogState state;
        HWND hwnd = create(state);
        state.pixelFormats.push_back({VideoPixelFormat::Nv12,60});
        UpdateAdvancedControlVisibility(&state);
        Require(SettingsCanStart(&state), "valid mocked capture mode can start");
        EnableWindow(state.startButton,FALSE);
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),0);
        Require(IsWindow(hwnd) && !state.accepted && !state.exclusiveProbeStop.load(),
            "IDOK cannot activate disabled Start or stop workers");
        key(hwnd,state.pixelFormatCombo,VK_RETURN);
        Require(IsWindow(hwnd) && !state.accepted, "Enter obeys disabled Start with production IDs");
        unchanged();
        state.pixelFormats.clear();
        EnableWindow(state.startButton,TRUE); // stale UI state must not bypass the data guard
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),0);
        Require(IsWindow(hwnd) && !state.accepted, "IDOK rejects enabled button with invalid capture capabilities");
        unchanged();
        for (int index = 0; index < llcv::settings_ui::kSettingsNavigationCount; ++index) {
            SendMessageW(state.tabControl,LB_SETCURSEL,index,0);
            SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_SETTINGS_TAB,LBN_SELCHANGE),
                reinterpret_cast<LPARAM>(state.tabControl));
            Require(state.activeTab == llcv::settings_ui::SettingsTabFromNavigationIndex(index) &&
                !IsWindowEnabled(state.startButton), "navigation preserves unsupported-mode Start gate");
            if (state.activeTab == SettingsTab::VideoWindow)
                Require(!IsWindowEnabled(state.pixelFormatCombo) && !IsWindowEnabled(state.frameRateCombo),
                    "navigation preserves unsupported format/FPS gates");
        }
        SendMessageW(state.audioOnlyCheck,BM_SETCHECK,BST_CHECKED,0);
        UpdateAdvancedControlVisibility(&state);
        Require(SettingsCanStart(&state) && IsWindowEnabled(state.startButton),
            "audio-only Shared can start without video capabilities");
        SendMessageW(state.audioCombo,CB_SETCURSEL,1,0);
        UpdateAdvancedControlVisibility(&state);
        Require(!SettingsCanStart(&state) && !IsWindowEnabled(state.startButton),
            "audio-only still requires Exclusive verification");
        SendMessageW(state.audioCombo,CB_SETCURSEL,0,0);
        state.activeTab = SettingsTab::VideoWindow;
        state.pixelFormats.push_back({VideoPixelFormat::Nv12,60});
        UpdateAdvancedControlVisibility(&state);
        SetFocus(state.pixelFormatCombo);
        SendMessageW(state.pixelFormatCombo,CB_SHOWDROPDOWN,TRUE,0);
        Require(SendMessageW(state.pixelFormatCombo,CB_GETDROPPEDSTATE,0,0) != 0, "native dropdown opened");
        key(hwnd,state.pixelFormatCombo,VK_ESCAPE);
        Require(IsWindow(hwnd) && !state.accepted &&
            SendMessageW(state.pixelFormatCombo,CB_GETDROPPEDSTATE,0,0) == 0,
            "first Escape closes dropdown without cancelling settings");
        key(hwnd,state.pixelFormatCombo,VK_ESCAPE);
        Require(!IsWindow(hwnd) && !state.accepted, "second Escape cancels settings through IDCANCEL");
        unchanged();
    }
    {
        SettingsDialogState state;
        HWND hwnd = create(state);
        key(hwnd,state.cancelButton,VK_RETURN);
        Require(!IsWindow(hwnd) && !state.accepted, "Enter on production Cancel cancels, not starts");
        unchanged();
    }
    UnregisterClassW(wc.lpszClassName,wc.hInstance);
    if (IsWindow(previousForeground)) SetForegroundWindow(previousForeground);
    g_settings = savedSettings;
    g_suppressSettingsSave = savedSuppress;
    std::puts("PASS: real settings controller IDs, disabled/invalid Start guards, navigation, audio-only and native keyboard routing.");
}

struct AudioUiMessages {
    unsigned enables = 0, textWrites = 0, resets = 0, paints = 0, directDraws = 0;
};
static LRESULT CALLBACK CountAudioUiMessages(HWND hwnd, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR reference) {
    auto& count = *reinterpret_cast<AudioUiMessages*>(reference);
    if (message == WM_ENABLE && wParam) ++count.enables;
    if (message == WM_SETTEXT) ++count.textWrites;
    if (message == CB_RESETCONTENT) ++count.resets;
    if (message == WM_PAINT) ++count.paints;
    // Native STATIC can draw straight to a DC during WM_SETTEXT/WM_ENABLE,
    // without sending WM_PAINT. Catch visible intermediate text draws too.
    if (message == WM_CTLCOLORSTATIC && IsWindowVisible(hwnd)) ++count.directDraws;
    return DefSubclassProc(hwnd, message, wParam, lParam);
}
static void TestSettingsAudioTransitions() {
    const auto savedSettings = g_settings;
    const bool savedSuppress = g_suppressSettingsSave;
    g_suppressSettingsSave = true;
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = SettingsControllerTestProc;
    wc.lpszClassName = L"LLCV_SETTINGS_AUDIO_TRANSITION_TEST";
    Require(RegisterClassW(&wc) != 0, "audio transition test class");
    unsigned transitions = 0;
    for (bool light : {false, true}) for (UINT dpi : {96u, 144u}) {
        SettingsDialogState state;
        state.activeTab = SettingsTab::Audio;
        state.asioAvailable = true; // UI-only; no real driver is enumerated
        state.probeReady = true;
        state.audioEndpoints.resize(1);
        state.audioEndpoints[0].id = L"fake-endpoint";
        state.audioEndpoints[0].name = L"Fixture speakers";
        state.audioEndpoints[0].isDefault = true;
        state.exclusiveEndpointResults.resize(1);
        state.exclusiveEndpointResults[0].state = ExclusiveEndpointState::Testing;
        // Simulate a worker in progress: no hardware enumeration or opening.
        state.exclusiveScanRunning = true;
        HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"Audio transition fixture", WS_POPUP | WS_CLIPCHILDREN,
            -32000, -32000, SettingsPixels(1000,dpi), SettingsPixels(650,dpi),
            nullptr, nullptr, wc.hInstance, &state);
        Require(hwnd != nullptr, "audio transition fixture");
        AppSettings initialSettings{};
        initialSettings.consoleSurround51 = true;
        const llcv::settings_ui::SettingsControlInitialValues initial{
            initialSettings, false, true, kAppVersionLabel, VideoPreset::R1920x1080,
            {}, {}, kVideoPresets, kPcmQueueOptionsMs, {}};
        const llcv::settings_ui::SettingsControlPopulation population{
            &state,
            [](void* s) { PopulateAudioOutputCombo(static_cast<SettingsDialogState*>(s)); },
            [](void* s) { PopulateSettingsBufferCombo(static_cast<SettingsDialogState*>(s)); },
            [](void*) {}};
        llcv::settings_ui::CreateSettingsDialogControls(&state, hwnd, wc.hInstance, initial, population);
        ApplySettingsFont(&state, hwnd, dpi);
        LayoutSettingsControls(&state, dpi);
        SendMessageW(state.themeCombo, CB_SETCURSEL, light ? 1 : 0, 0);
        UpdateAdvancedControlVisibility(&state);
        Require(state.theme.Attach(hwnd, &state), "audio transition theme");
        state.theme.RefreshControls(dpi);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        AudioUiMessages button, hint, status, output, buffer, owner;
        for (auto pair : {std::pair{state.exclusiveTestButton,&button},
                          std::pair{state.surround51Hint,&hint}, std::pair{state.audioStatus,&status},
                          std::pair{state.audioOutputCombo,&output}, std::pair{state.bufferCombo,&buffer},
                          std::pair{hwnd,&owner}})
            Require(SetWindowSubclass(pair.first, CountAudioUiMessages, 99,
                reinterpret_cast<DWORD_PTR>(pair.second)) != FALSE, "audio message observer");
        auto selectMode = [&](int mode) {
            SendMessageW(state.audioCombo, CB_SETCURSEL, mode, 0);
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SETTINGS_AUDIO,CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(state.audioCombo));
        };
        for (int i = 0; i < 100; ++i) {
            button = {}; hint = {}; status = {}; owner = {};
            const bool exclusive = i % 2 == 0;
            selectMode(exclusive ? 1 : 0);
            Require(button.enables == 0, "running scan button never transiently enables during mode change");
            Require(IsWindowEnabled(state.surround51Check) == !exclusive &&
                    IsWindowEnabled(state.surround51Hint) == !exclusive,
                    "surround state follows final output mode");
            Require(SendMessageW(state.surround51Check,BM_GETCHECK,0,0) == BST_CHECKED,
                    "mode changes retain surround preference");
            Require(status.textWrites <= 1, "one final status per mode change");
            Require(owner.directDraws == 0, "no visible direct text draws mid-transition");
            Require(!state.exclusiveProbeThread.joinable() && state.exclusiveScanRunning,
                    "repeated selection reuses pending scan");
            RedrawWindow(hwnd, nullptr, nullptr, RDW_ALLCHILDREN | RDW_UPDATENOW);
            Require(button.paints <= 1 && hint.paints <= 1 && status.paints <= 1,
                    "mode transition coalesces to one final paint per affected control");
            ++transitions;
        }
        selectMode(1);
        button = {}; output = {}; buffer = {}; status = {};
        for (int i = 0; i < 25; ++i) {
            UpdateAdvancedControlVisibility(&state);
            UpdateExclusiveProbeControl(&state);
        }
        Require(button.enables == 0 && button.textWrites == 0,
                "unchanged busy state never enables or rewrites the scan button");
        SendMessageW(hwnd, WM_AUDIOCLIENT3_PROBE_COMPLETE, 0, 0);
        Require(output.resets == 0 && buffer.resets == 0 && status.textWrites == 0,
                "late Shared completion leaves Exclusive UI unchanged");
        selectMode(0);
        // An Exclusive result arriving after switching back must not rebuild
        // Shared fields or silently replace the user's buffer selection.
        output = {}; buffer = {}; status = {};
        const int oldBuffer = state.selectedBufferMs;
        ExclusiveEndpointProbeResult result{};
        result.probe.compatible = true;
        result.probe.requestedFrames = 240;
        QueueExclusiveEndpointProbeResult(&state, result);
        SendMessageW(hwnd, WM_EXCLUSIVE_ENDPOINT_PROBE_COMPLETE, 0, 0);
        SendMessageW(hwnd, WM_EXCLUSIVE_SCAN_COMPLETE, 0, 0);
        Require(!state.exclusiveScanRunning && state.exclusiveScanCompleted == 1 &&
                state.exclusiveEndpointResults[0].state == ExclusiveEndpointState::Supported,
                "late results retained and completion clears running flag");
        Require(output.resets == 0 && buffer.resets == 0 && status.textWrites == 0 &&
                state.selectedBufferMs == oldBuffer,
                "late Exclusive results never repaint or modify Shared choices");
        selectMode(2);
        Require(!IsWindowEnabled(state.bufferCombo) && !IsWindowEnabled(state.surround51Check),
                "ASIO keeps its driver-owned buffer and disabled surround controls");
        output = {}; buffer = {}; status = {};
        SendMessageW(hwnd, WM_EXCLUSIVE_ENDPOINT_PROBE_COMPLETE, 0, 0);
        SendMessageW(hwnd, WM_AUDIOCLIENT3_PROBE_COMPLETE, 0, 0);
        Require(output.resets == 0 && buffer.resets == 0 && status.textWrites == 0 &&
                !IsWindowEnabled(state.bufferCombo), "late WASAPI messages leave ASIO fields untouched");
        selectMode(1);
        Require(!state.exclusiveProbeThread.joinable() && !state.exclusiveScanRunning &&
                IsWindowEnabled(state.exclusiveTestButton), "completed scan reused on revisit");
        // Start the real asynchronous path with an empty endpoint list: it
        // posts completion without touching a driver, still exercising the
        // initial idle-to-scanning transition and worker ownership.
        selectMode(0);
        state.audioEndpoints.clear();
        state.exclusiveEndpointResults.clear();
        state.exclusiveScanCompleted = 0;
        button = {}; status = {}; owner = {};
        selectMode(1);
        Require(button.enables == 0 && status.textWrites == 1 && owner.directDraws == 0,
                "first scan shows only its final busy state, not an enabled button or intermediate status");
        Require(state.exclusiveProbeThread.joinable(), "empty scan exercises real worker start");
        state.exclusiveProbeThread.join();
        MSG completion{};
        Require(PeekMessageW(&completion,hwnd,WM_EXCLUSIVE_SCAN_COMPLETE,
                WM_EXCLUSIVE_SCAN_COMPLETE,PM_REMOVE) != FALSE, "empty scan posted completion");
        DispatchMessageW(&completion);
        Require(!state.exclusiveScanRunning && IsWindowEnabled(state.exclusiveTestButton),
                "worker completion restores retry button");
        DestroyWindow(hwnd);
        for (HFONT font : state.uiFonts) DeleteObject(font);
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_settings = savedSettings;
    g_suppressSettingsSave = savedSuppress;
    std::printf("PASS: %u real-controller audio mode transitions; scan gates, late results and redraw coalescing.\n", transitions);
}

static std::vector<BYTE> OutputPixels(DirectD3D11Renderer& renderer) {
    D3D11_TEXTURE2D_DESC desc{};
    renderer.backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy;
    Check(renderer.device->CreateTexture2D(&desc, nullptr, &copy), "readback texture");
    renderer.context->CopyResource(copy.Get(), renderer.backBuffer);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(renderer.context->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &mapped), "readback outside timing");
    std::vector<BYTE> pixels(static_cast<size_t>(desc.Width)*desc.Height*4);
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = static_cast<const BYTE*>(mapped.pData) + y * mapped.RowPitch;
        std::memcpy(pixels.data()+static_cast<size_t>(y)*desc.Width*4,row,static_cast<size_t>(desc.Width)*4);
    }
    renderer.context->Unmap(copy.Get(), 0);
    return pixels;
}
static std::atomic<int> modeQueryCalls{0};
static void TestSettingsModeFlow() {
    const auto saved = g_settings;
    g_testVideoCapabilityProbe = [](const std::wstring&, int width, int, HRESULT* status) {
        ++modeQueryCalls;
        std::this_thread::sleep_for(std::chrono::milliseconds(80)); // slow-driver simulation
        *status = S_OK;
        return std::vector<PixelFormatSupport>{{VideoPixelFormat::Nv12, width == 1280 ? 144 : 60}};
    };
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = SettingsControllerTestProc;
    wc.lpszClassName = L"LLCV_SETTINGS_ASYNC_TEST";
    Require(RegisterClassW(&wc) != 0, "async test class");
    SettingsDialogState state;
    state.activeTab = SettingsTab::VideoWindow;
    HWND window = CreateWindowW(wc.lpszClassName, L"Async fixture", WS_POPUP,
        0,0,1000,650,nullptr,nullptr,wc.hInstance,&state);
    auto combo = [&](int id) {
        return CreateWindowW(L"COMBOBOX",L"",WS_CHILD | CBS_DROPDOWNLIST,
            0,0,200,120,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);
    };
    state.videoCombo = combo(IDC_SETTINGS_VIDEO);
    state.vsrCaptureCombo = combo(IDC_SETTINGS_VSR_CAPTURE);
    state.pixelFormatCombo = combo(IDC_SETTINGS_PIXEL_FORMAT);
    state.frameRateCombo = combo(IDC_SETTINGS_FRAME_RATE);
    state.vsrCheck = CreateWindowW(L"BUTTON",L"",WS_CHILD | BS_AUTOCHECKBOX,
        0,0,200,30,window,nullptr,wc.hInstance,nullptr);
    state.startButton = CreateWindowW(L"BUTTON",L"Start",WS_CHILD,
        0,0,100,30,window,nullptr,wc.hInstance,nullptr);
    for (const auto& preset : kVideoPresets) {
        SendMessageW(state.videoCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(preset.label));
        SendMessageW(state.vsrCaptureCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(preset.label));
    }
    SendMessageW(state.vsrCheck,BM_SETCHECK,BST_CHECKED,0);
    SendMessageW(state.vsrCaptureCombo,CB_SETCURSEL,0,0);
    SendMessageW(state.videoCombo,CB_SETCURSEL,2,0);
    auto complete = [&] {
        const auto deadline = GetTickCount64() + 2000;
        while (state.videoModesPending && GetTickCount64() < deadline) {
            MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);
            MSG msg{}; while (PeekMessageW(&msg,window,0,0,PM_REMOVE)) DispatchMessageW(&msg);
        }
        Require(!state.videoModesPending && IsWindowEnabled(state.startButton), "current modes arrive asynchronously");
    };
    PopulatePixelFormatCombo(&state);
    Require(state.videoModesPending && !IsWindowEnabled(state.startButton), "cannot start with stale modes");
    complete();
    Require(modeQueryCalls == 1 && state.pixelFormats.front().selectedFps == 144, "FPS follows capture size");
    for (int i = 0; i < 50; ++i) {
        SendMessageW(state.videoCombo,CB_SETCURSEL,1 + i % 3,0);
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDC_SETTINGS_VIDEO,CBN_SELCHANGE),0);
    }
    Require(modeQueryCalls == 1 && !state.videoModesPending, "display changes never rescan or clear FPS");
    SendMessageW(state.vsrCheck,BM_SETCHECK,BST_UNCHECKED,0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDC_SETTINGS_VSR,BN_CLICKED),0);
    complete();
    SendMessageW(state.vsrCheck,BM_SETCHECK,BST_CHECKED,0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDC_SETTINGS_VSR,BN_CLICKED),0);
    Require(modeQueryCalls == 2 && !state.videoModesPending &&
        state.pixelFormats.front().selectedFps == 144, "revisiting capture mode uses cached capabilities");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDC_SETTINGS_VIDEO_REFRESH,BN_CLICKED),0);
    complete();
    Require(modeQueryCalls == 3, "only explicit refresh repeats the same query");
    state.videoModeCache.reset();
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName,wc.hInstance);
    g_testVideoCapabilityProbe = nullptr;
    g_settings = saved;
    std::puts("PASS: production settings flow, slow driver, 50 display changes without rescans, cached capture/FPS and refresh.");
}

static uint64_t OutputHash(DirectD3D11Renderer& renderer) {
    uint64_t hash = 14695981039346656037ull;
    for (const auto value : OutputPixels(renderer)) hash = (hash ^ value) * 1099511628211ull;
    return hash;
}
static void PrintPixelDifference(const std::vector<BYTE>& a, const std::vector<BYTE>& b,
                                 const char* label) {
    Require(a.size() == b.size() && !a.empty(), "pixel comparison dimensions");
    uint64_t changed = 0, total = 0;
    int maximum = 0;
    for (size_t i = 0; i < a.size(); i += 4) {
        bool different = false;
        for (size_t channel = 0; channel < 3; ++channel) {
            const int delta = std::abs(static_cast<int>(a[i+channel])-b[i+channel]);
            different |= delta != 0; total += delta; maximum = (std::max)(maximum,delta);
        }
        changed += different;
    }
    std::printf("PIXELS %s changed=%llu/%llu (%.4f%%) RGB_MAE=%.6f max=%d/255\n",
        label,changed,static_cast<uint64_t>(a.size()/4),100.0*changed/(a.size()/4),
        static_cast<double>(total)/(a.size()/4*3),maximum);
}

static void TestNativeControls(HWND window, int width, int height, const std::vector<BYTE>& pixels) {
    g_vsrMode = llcv::vsr::Mode::Disabled;
    DirectD3D11Renderer renderer;
    Check(renderer.initialize(window,width,height,60,VideoPixelFormat::Nv12), "native controls init");
    Require(renderer.vsrEligible && renderer.vsrState == llcv::vsr::State::Untouched,
        "native controls eligible but default processing remains untouched");
    const auto* device = renderer.device;
    const auto* processor = renderer.processor;
    const auto* swapchain = renderer.swapChain;
    auto render = [&] {
        // Warm repeated static input outside any timing measurement.
        for (int i = 0; i < 12; ++i) {
            renderer.upload(pixels.data(),static_cast<UINT32>(width));
            D3D11_VIDEO_PROCESSOR_STREAM stream{};
            stream.Enable = TRUE; stream.pInputSurface = renderer.inputViews[renderer.activeUploadSurface];
            Check(renderer.videoContext->VideoProcessorBlt(renderer.processor,renderer.outputView,0,1,&stream),
                "native live control blit");
            renderer.context->Flush();
        }
        return OutputPixels(renderer);
    };
    const auto baseline = render();
    for (int pair = 0; pair < 3; ++pair) {
        g_vsrMode = llcv::vsr::Mode::On;
        Check(renderer.applyVsrMode(), "native live ON");
        Require(renderer.vsrState == llcv::vsr::State::Requested, "native live ON accepted");
        PrintPixelDifference(baseline,render(),"native live default vs ON");
        g_vsrMode = llcv::vsr::Mode::Off;
        Check(renderer.applyVsrMode(), "native live OFF");
        Require(renderer.vsrState == llcv::vsr::State::Off, "native live OFF accepted");
        Require(render() == baseline, "native OFF restores untouched default pixels exactly");
        Require(renderer.device == device && renderer.processor == processor && renderer.swapChain == swapchain,
            "native live toggles preserve all rendering resources");
    }
    std::puts("PASS: native default/OFF exact equality and three same-resource ON/OFF controls.");
    g_vsrMode = llcv::vsr::Mode::Disabled;
}

static void TestLiveToggle(HWND window, int width, int height, const std::vector<BYTE>& pixels) {
    g_vsrMode.store(llcv::vsr::Mode::Disabled);
    DirectD3D11Renderer renderer;
    Check(renderer.initialize(window,width,height,60,VideoPixelFormat::Nv12), "toggle renderer init");
    const auto* originalDevice = renderer.device;
    const auto* originalProcessor = renderer.processor;
    const auto* originalSwapchain = renderer.swapChain;
    const bool originalVsrEligible = renderer.vsrEligible;
    const auto originalGeneration = g_outputConfigurationGeneration.load();
    const auto originalRendererGeneration = renderer.outputConfigurationGeneration;
    auto renderHash = [&] {
        renderer.upload(pixels.data(), static_cast<UINT32>(width));
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE; stream.pInputSurface = renderer.inputViews[renderer.activeUploadSurface];
        Check(renderer.videoContext->VideoProcessorBlt(renderer.processor,renderer.outputView,0,1,&stream),
            "same-resource toggle blit");
        return OutputHash(renderer);
    };
    const uint64_t baseline = renderHash();
    Require(!HandleVsrTestKey(VK_F5,0), "other shortcuts untouched");
    g_settings.audioOnly = true;
    Require(HandleVsrTestKey(VK_F6,0) && g_vsrMode.load() == llcv::vsr::Mode::Disabled,
        "audio-only does not change VSR request");
    g_settings.audioOnly = false;
    for (int i = 0; i < 6; ++i) {
        const auto stateBeforeKey = renderer.vsrState;
        Require(HandleVsrTestKey(VK_F6,0), "F6 handled");
        Require(g_settings.vsrEnabled, "F6 ON updates saved preference");
        Require(g_vsrMode.load() == llcv::vsr::Mode::On && renderer.vsrState == stateBeforeKey,
            "UI updates request only, not D3D state");
        Require(HandleVsrTestKey(VK_F6,LPARAM{1} << 30) && g_vsrMode.load() == llcv::vsr::Mode::On,
            "held F6 ignores auto-repeat");
        Check(renderer.applyVsrMode(), "ON on render thread");
        Require(renderer.cachedOverlayGeneration == 0, "applied F6 invalidates Tab status cache");
        Require(renderer.vsrState == (renderer.vsrEligible ? llcv::vsr::State::Requested
                                                          : llcv::vsr::State::Bypassed), "ON state");
        const uint64_t enabled = renderHash();
        std::printf("live pair=%d eligible=%d ON differs=%d\n",i,renderer.vsrEligible,enabled != baseline);
        // HUD must use the acknowledged render state, not claim activation.
        Require(g_transientHudContent.load() == (renderer.vsrEligible ? TransientHudContent::VsrOn
            : TransientHudContent::VsrUnavailable), "acknowledged HUD");
        const auto overlayGeneration = g_overlayGeneration.load();
        Check(renderer.applyVsrMode(), "unchanged mode no-op");
        Require(g_overlayGeneration.load() == overlayGeneration, "no per-frame status churn");
        HandleVsrTestKey(VK_F6,0);
        Check(renderer.applyVsrMode(), "OFF on render thread");
        Require(!g_settings.vsrEnabled, "F6 OFF updates saved preference");
        Require(renderHash() == baseline, "live OFF restores original pixels exactly");
        Require(renderer.device == originalDevice && renderer.processor == originalProcessor &&
            renderer.swapChain == originalSwapchain &&
            renderer.outputConfigurationGeneration == originalRendererGeneration &&
            g_outputConfigurationGeneration.load() == originalGeneration, "toggle never rebuilds output");
    }
    HandleVsrTestKey(VK_F6,0); HandleVsrTestKey(VK_F6,0);
    Check(renderer.applyVsrMode(), "rapid toggles coalesce to last request");
    Require(g_vsrMode.load() == llcv::vsr::Mode::Off && renderHash() == baseline, "rapid OFF result");
    // Exercise actual Present/HUD path (a hidden window may be occluded).
    HandleVsrTestKey(VK_F6,0);
    Check(renderer.presentUploaded(), "production Present handles pending toggle");
    Require(renderer.vsrAppliedMode == llcv::vsr::Mode::On, "Present consumes request");
    HandleVsrTestKey(VK_F6,0);
    Check(renderer.presentUploaded(), "OFF while occluded is consumed");
    Require(renderer.vsrAppliedMode == llcv::vsr::Mode::Off, "occlusion does not lose request");
    HandleVsrTestKey(VK_F6,0); // preserve ON intent through real output changes
    Require(SetWindowPos(window,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE) != 0,
        "hidden 1:1 output");
    Check(renderer.initialize(window,width,height,60,VideoPixelFormat::Nv12), "recreate 1:1");
    Require(renderer.vsrEligible == originalVsrEligible &&
        renderer.vsrAppliedMode == llcv::vsr::Mode::On &&
        renderer.vsrState == (renderer.vsrEligible ? llcv::vsr::State::Requested : llcv::vsr::State::Bypassed) &&
        g_transientHudContent.load() == (renderer.vsrEligible ? TransientHudContent::VsrOn
            : TransientHudContent::VsrUnavailable), "native-size ON intent and acknowledged HUD");
    Require(SetWindowPos(window,nullptr,0,0,width/2,height/2,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE) != 0,
        "hidden downscaled output");
    Check(renderer.initialize(window,width,height,60,VideoPixelFormat::Nv12), "recreate downscale");
    Require(!renderer.vsrEligible && renderer.vsrState == llcv::vsr::State::Bypassed &&
        g_transientHudContent.load() == TransientHudContent::VsrUnavailable, "downscale bypass with visible explanation");
    Require(SetWindowPos(window,nullptr,0,0,width*2,height*2,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE) != 0,
        "restore hidden upscale output");
    Check(renderer.initialize(window,width,height,60,VideoPixelFormat::Nv12), "recreate upscale");
    Require(renderer.vsrAppliedMode == llcv::vsr::Mode::On &&
        renderer.vsrState == (renderer.vsrEligible ? llcv::vsr::State::Requested : llcv::vsr::State::Bypassed),
        "ON intent reapplied after actual resize");
    const auto savedLanguage = g_settings.uiLanguage;
    for (const auto language : {UiLanguage::Korean, UiLanguage::English}) {
        g_settings.uiLanguage = language;
        for (const auto content : {TransientHudContent::VsrPending, TransientHudContent::VsrOn,
                TransientHudContent::VsrOff, TransientHudContent::VsrUnavailable,
                TransientHudContent::VsrRejected, TransientHudContent::VsrFailed}) {
            ShowTransientHud(content);
            Check(renderer.refreshOverlayLayouts(), "bilingual VSR overlay");
            DWRITE_TEXT_METRICS metrics{};
            Check(renderer.volumeTextLayout->GetMetrics(&metrics), "VSR HUD text metrics");
            std::printf("HUD language=%d content=%d width=%.2f height=%.2f lines=%u\n",
                static_cast<int>(language), static_cast<int>(content), metrics.width, metrics.height, metrics.lineCount);
            Require(metrics.height <= 62.0f && metrics.width <= 228.0f && metrics.lineCount <= 2,
                "VSR messages fit existing HUD without clipping");
        }
    }
    g_settings.uiLanguage = savedLanguage;
    g_vsrMode = llcv::vsr::Mode::Disabled;
}

static void TestCenteredPixelPerfect(HWND window, int width, int height, const std::vector<BYTE>& pixels) {
    const bool savedPixelPerfect = g_settings.pixelPerfect;
    const auto savedScaling = g_settings.scalingMode;
    const bool savedFullscreen = g_fullscreen.load();
    const auto savedMode = g_vsrMode.load();
    g_settings.pixelPerfect = true;
    g_settings.scalingMode = ScalingMode::Sharp; // Native-size output must not add edge enhancement.
    g_fullscreen = true;
    g_vsrMode = llcv::vsr::Mode::Disabled;
    Require(SetWindowPos(window,nullptr,0,0,width*2,height*2,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE) != 0,
        "centered pixel-perfect hidden output");
    DirectD3D11Renderer renderer;
    Check(renderer.initialize(window,width,height,60,VideoPixelFormat::Nv12), "centered pixel-perfect init");
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    Check(renderer.device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)), "centered adapter device");
    Check(dxgiDevice->GetAdapter(&adapter), "centered adapter");
    Check(adapter->GetDesc(&desc), "centered adapter description");
    const bool expectedEligible = llcv::vsr::Eligible(desc.VendorId,DXGI_FORMAT_NV12,false,
        static_cast<UINT>(width),static_cast<UINT>(height),static_cast<UINT>(width),static_cast<UINT>(height));
    Require(renderer.pixelPerfectFullscreen && renderer.pixelPerfectBorders &&
        renderer.outputWidth == static_cast<UINT>(width*2) &&
        renderer.outputHeight == static_cast<UINT>(height*2) &&
        renderer.vsrEligible == expectedEligible && !renderer.sharpScalingActive &&
        renderer.vsrState == llcv::vsr::State::Untouched,
        "centered native policy uses video rectangle, not larger backbuffer");
    const RECT sourceExpected{0,0,width,height};
    const RECT nativeExpected{width/2,height/2,width/2+width,height/2+height};
    auto requireRects = [&](const RECT& destination, const RECT& target) {
        BOOL sourceEnabled = FALSE, destEnabled = FALSE, targetEnabled = FALSE;
        RECT source{}, dest{}, output{};
        renderer.videoContext->VideoProcessorGetStreamSourceRect(renderer.processor,0,&sourceEnabled,&source);
        renderer.videoContext->VideoProcessorGetStreamDestRect(renderer.processor,0,&destEnabled,&dest);
        renderer.videoContext->VideoProcessorGetOutputTargetRect(renderer.processor,&targetEnabled,&output);
        Require(sourceEnabled && EqualRect(&source,&sourceExpected) &&
            destEnabled && EqualRect(&dest,&destination) && targetEnabled && EqualRect(&output,&target),
            "source/destination/target rectangles preserve pixel-perfect placement");
    };
    const RECT largeTarget{0,0,width*2,height*2};
    requireRects(nativeExpected,largeTarget);
    const auto* device = renderer.device;
    const auto* processor = renderer.processor;
    const auto* swapchain = renderer.swapChain;
    auto renderNativeRegion = [&] {
        renderer.upload(pixels.data(),static_cast<UINT32>(width));
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE; stream.pInputSurface = renderer.inputViews[renderer.activeUploadSurface];
        Check(renderer.videoContext->VideoProcessorBlt(renderer.processor,renderer.outputView,0,1,&stream),
            "centered native blit");
        const auto output = OutputPixels(renderer);
        std::vector<BYTE> region(static_cast<size_t>(width)*height*4);
        // Direct test blits need not clear the letterbox region. Compare only
        // the queried, fully written video rectangle, never stale border RGB.
        for (int row = 0; row < height; ++row) {
            const size_t sourceOffset = (static_cast<size_t>(nativeExpected.top+row)*renderer.outputWidth+
                nativeExpected.left)*4;
            std::memcpy(region.data()+static_cast<size_t>(row)*width*4,
                output.data()+sourceOffset,static_cast<size_t>(width)*4);
        }
        return region;
    };
    const auto baseline = renderNativeRegion();
    for (int pair = 0; pair < 3; ++pair) {
        g_vsrMode = llcv::vsr::Mode::On;
        Check(renderer.applyVsrMode(), "centered native ON");
        Require(renderer.vsrState == (expectedEligible ? llcv::vsr::State::Requested
            : llcv::vsr::State::Bypassed), "centered native ON acknowledged");
        requireRects(nativeExpected,largeTarget);
        Require(renderer.vsrInputWidth == static_cast<UINT>(width) &&
            renderer.vsrInputHeight == static_cast<UINT>(height) &&
            renderer.vsrDisplayWidth == static_cast<UINT>(width) &&
            renderer.vsrDisplayHeight == static_cast<UINT>(height),
            "Tab VSR sizes exclude fullscreen pixel-perfect borders");
        PrintPixelDifference(baseline,renderNativeRegion(),"centered native default vs ON");
        g_vsrMode = llcv::vsr::Mode::Off;
        Check(renderer.applyVsrMode(), "centered native OFF");
        Require(renderer.vsrState == (expectedEligible ? llcv::vsr::State::Off
            : llcv::vsr::State::Bypassed), "centered native OFF acknowledged");
        Require(renderNativeRegion() == baseline, "centered native OFF restores written video region");
        requireRects(nativeExpected,largeTarget);
        Require(renderer.device == device && renderer.processor == processor && renderer.swapChain == swapchain,
            "centered native toggles preserve rendering resources");
    }
    g_vsrMode = llcv::vsr::Mode::On;
    Require(SetWindowPos(window,nullptr,0,0,width/2,height/2,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE) != 0,
        "oversized pixel-perfect input hidden output");
    Check(renderer.initialize(window,width,height,60,VideoPixelFormat::Nv12), "pixel-perfect downscale init");
    const RECT smallTarget{0,0,width/2,height/2};
    requireRects(smallTarget,smallTarget);
    Require(renderer.pixelPerfectFullscreen && !renderer.pixelPerfectBorders && !renderer.vsrEligible &&
        !renderer.sharpScalingActive && renderer.vsrState == llcv::vsr::State::Bypassed &&
        renderer.vsrAppliedMode == llcv::vsr::Mode::On,
        "pixel-perfect downscale bypasses VSR while retaining ON intent");
    renderer.reset();
    g_settings.pixelPerfect = savedPixelPerfect;
    g_settings.scalingMode = savedScaling;
    g_fullscreen = savedFullscreen;
    g_vsrMode = savedMode;
    std::puts("PASS: centered fullscreen 1:1 rectangles, ON/OFF native pixels, resources, and oversized-input bypass.");
}

static void TestSplitResolutionRenderer(HWND window) {
    const auto saved = g_settings;
    const bool fullscreen = g_fullscreen.load();
    for (const auto display : {VideoPreset::R2560x1440, VideoPreset::R3840x2160}) {
        g_settings.videoPreset = display;
        g_settings.vsrCapturePreset = VideoPreset::R1280x720;
        g_settings.vsrEnabled = true;
        g_settings.pixelPerfect = true; // UI now locks DISPLAY size, not source pixels.
        g_settings.audioOnly = false;
        LatchVideoResolutionPlan();
        const auto size = InitialClientPixelsForMonitor(nullptr);
        SetWindowPos(window, nullptr, 0, 0, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
        g_fullscreen = true;
        g_vsrMode = llcv::vsr::Mode::Off;
        DirectD3D11Renderer renderer;
        Check(renderer.initialize(window, 1280, 720, 60, VideoPixelFormat::Nv12), "split renderer");
        Require(!renderer.pixelPerfectFullscreen && renderer.outputWidth == static_cast<UINT>(size.cx) &&
            renderer.outputHeight == static_cast<UINT>(size.cy) && renderer.vsrEligible,
            "split VSR display fills chosen output even with display lock");
        auto* chain = renderer.swapChain;
        for (int i = 0; i < 12; ++i) {
            HandleVsrTestKey(VK_F6, 0);
            Check(renderer.applyVsrMode(false), "split F6 request");
            Require(renderer.swapChain == chain && CurrentCapturePreset().width == 1280 &&
                renderer.outputWidth == static_cast<UINT>(size.cx) && renderer.outputHeight == static_cast<UINT>(size.cy),
                "split F6 preserves swap chain and dimensions");
        }
    }
    g_settings = saved;
    g_fullscreen = fullscreen;
    g_resolutionPlanLatched = false;
    g_vsrResolutionPlan = false;
    g_vsrMode = llcv::vsr::Mode::Disabled;
}

#include "VsrTimingValidation.inl"

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--timing-validation") return ValidateVsrTiming(argc, argv);
    TestPolicy();
    const bool nativeColor = argc > 1 && std::string(argv[1]) == "--bench-native-color";
    const bool native = nativeColor || (argc > 1 && std::string(argv[1]) == "--bench-native");
    const bool bench720To4k = argc > 1 && std::string(argv[1]) == "--bench-720p-4k";
    const bool bench720 = bench720To4k || (argc > 1 && std::string(argv[1]) == "--bench-720p");
    const bool benchmark = native || bench720 || (argc > 1 && std::string(argv[1]) == "--bench");
    const bool reverse = argc > 2 && std::string(argv[2]) == "--reverse";
    const bool hd720 = argc > 1 && std::string(argv[1]) == "--720p";
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM");
    if (argc > 1 && std::string(argv[1]) == "--settings-controller") {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};
        Require(InitCommonControlsEx(&controls) != FALSE, "controller common controls");
        TestSettingsController();
        TestSettingsAudioTransitions();
        TestSettingsModeFlow();
        CoUninitialize();
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--probe") {
        const auto mode = g_vsrMode.load();
        for (int i = 0; i < 3; ++i) {
            const auto result = llcv::vsr::ProbeSupport();
            std::printf("probe=%d state=%d HRESULT=0x%08X (global setting/activation unverified)\n",
                i, static_cast<int>(result.status), static_cast<unsigned>(result.result));
            Require(g_vsrMode.load() == mode, "probe never changes viewer VSR intent");
            Require(result.status != llcv::vsr::SupportProbe::Status::RequestSupported ||
                    result.result == S_OK, "request support must have exact success");
        }
        CoUninitialize();
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"LLCV_VSR_TEST";
    Require(RegisterClassW(&wc) != 0, "test class");
    const int width = bench720 ? 1280 : benchmark ? 1920 : hd720 ? 1280 : 640;
    const int height = bench720 ? 720 : benchmark ? 1080 : hd720 ? 720 : 360;
    const int targetWidth = native ? width : bench720To4k ? 3840 : hd720 ? 1920 : width*2;
    const int targetHeight = native ? height : bench720To4k ? 2160 : hd720 ? 1080 : height*2;
    HWND window = CreateWindowW(wc.lpszClassName, L"Hidden VSR test", WS_POPUP,
        0,0,targetWidth,targetHeight,
        nullptr,nullptr,wc.hInstance,nullptr);
    Require(window != nullptr, "hidden window");
    if (benchmark) std::printf("BENCH input=%dx%d output=%dx%d fps=60 reverse=%d; synthetic NV12, "
        "Smooth, no Present, driver quality unchanged/unverified\n",
        width,height,targetWidth,targetHeight,reverse);
    g_settings.pixelPerfect = false; g_settings.scalingMode = ScalingMode::Smooth;
    g_settings.presentationMode = PresentationMode::AllowTearing;
    g_suppressSettingsSave = true;
    std::vector<BYTE> pixels(static_cast<size_t>(width)*height*3/2, 128);
    if (nativeColor) for (int y = 0; y < height/2; ++y) for (int x = 0; x < width; x += 2) {
        const size_t offset = static_cast<size_t>(width)*height+static_cast<size_t>(y)*width+x;
        pixels[offset] = static_cast<BYTE>(96+((x/32+y/16)%5)*16);
        pixels[offset+1] = static_cast<BYTE>(96+((x/48+y/24)%5)*16);
    }
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        pixels[static_cast<size_t>(y)*width+x] = static_cast<BYTE>(16 +
            ((x / 3 + y / 5 + ((x/40+y/40)%2)*90) % 220));
    const int phases = benchmark ? 6 : 3;
    uint64_t defaultHash = 0;
    std::vector<BYTE> firstOff, firstOn;
    for (int phase = 0; phase < phases; ++phase) {
        g_vsrMode = benchmark ? ((static_cast<bool>(phase % 2) != reverse) ? llcv::vsr::Mode::On : llcv::vsr::Mode::Off)
            : (phase == 0 ? llcv::vsr::Mode::Disabled : llcv::vsr::Mode::Off);
        DirectD3D11Renderer renderer;
        const HRESULT init = renderer.initialize(window,width,height,60,VideoPixelFormat::Nv12);
        if (FAILED(init)) {
            std::printf("GPU/extension unavailable: 0x%08X (not a successful VSR test)\n", static_cast<unsigned>(init));
            return 77;
        }
        if (phase == 0 && !benchmark)
            Require(renderer.vsrState == llcv::vsr::State::Untouched, "default initialization untouched");
        if (benchmark && renderer.vsrState == llcv::vsr::State::Bypassed) {
            std::puts("SKIP: NVIDIA NV12 native/upscale route not available"); return 77;
        }
        if (benchmark) {
            Require(renderer.vsrEligible, "benchmark uses production eligibility without overrides");
            Require(renderer.outputWidth == static_cast<UINT>(targetWidth) &&
                    renderer.outputHeight == static_cast<UINT>(targetHeight), "exact benchmark output size");
            Require(renderer.vsrState == (g_vsrMode == llcv::vsr::Mode::On
                    ? llcv::vsr::State::Requested : llcv::vsr::State::Off),
                    "benchmark requires accepted ON/OFF requests");
        }
        // CPU instrumentation is identical for A/B. Small bounded warmup in
        // synthetic bench; interactive app excludes five seconds of frames.
        if (benchmark) Check(renderer.vsrTiming.Initialize(renderer.device,60), "CPU timer");
        // Independent, serialized completion cross-check. This forced readback
        // exists ONLY in this executable, never in the viewer. Its overhead
        // means these timings must not be described as actual display latency.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> completionPixel;
        llcv::vsr::Distribution completed;
        if (benchmark) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            Check(renderer.device->CreateTexture2D(&desc,nullptr,&completionPixel), "completion dependency");
        }
        const int frames = (native || bench720) ? 360 : benchmark ? 240 : 1;
        const int warmup = (native || bench720) ? 120 : 60;
        auto next = std::chrono::steady_clock::now();
        for (int frame = 0; frame < frames; ++frame) {
            if (benchmark) for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
                pixels[static_cast<size_t>(y)*width+x] = static_cast<BYTE>(16 +
                    (((x+frame*3) / 3 + y / 5 + (((x+frame*3)/40+y/40)%2)*90) % 220));
            const auto completionStart = std::chrono::steady_clock::now();
            renderer.upload(pixels.data(), static_cast<UINT32>(width));
            D3D11_VIDEO_PROCESSOR_STREAM stream{};
            stream.Enable = TRUE; stream.pInputSurface = renderer.inputViews[renderer.activeUploadSurface];
            renderer.vsrTiming.Begin(renderer.context);
            const HRESULT hr = renderer.videoContext->VideoProcessorBlt(renderer.processor,
                renderer.outputView,0,1,&stream);
            renderer.vsrTiming.End(renderer.context,SUCCEEDED(hr));
            Check(hr, "video processor blit");
            // Test-only submission: no Present to a hidden window. Not an
            // input-to-display benchmark. The application uses normal Present.
            renderer.context->Flush();
            if (benchmark) {
                const D3D11_BOX pixelBox{0,0,0,1,1,1};
                renderer.context->CopySubresourceRegion(completionPixel.Get(),0,0,0,0,
                    renderer.backBuffer,0,&pixelBox);
                D3D11_MAPPED_SUBRESOURCE mapped{};
                Check(renderer.context->Map(completionPixel.Get(),0,D3D11_MAP_READ,0,&mapped), "serialized completion");
                renderer.context->Unmap(completionPixel.Get(),0);
                if (frame >= warmup) completed.Add(std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-completionStart).count());
                next += std::chrono::microseconds(16667);
                std::this_thread::sleep_until(next);
            }
        }
        const uint64_t hash = OutputHash(renderer); // blocks only after benchmark
        if (benchmark) {
            auto current = OutputPixels(renderer);
            auto& first = g_vsrMode == llcv::vsr::Mode::On ? firstOn : firstOff;
            if (first.empty()) first = current;
            else PrintPixelDifference(first,current,g_vsrMode == llcv::vsr::Mode::On ? "ON repeat" : "OFF repeat");
            if (!firstOn.empty() && !firstOff.empty()) PrintPixelDifference(firstOff,firstOn,"OFF vs ON");
        }
        if (!benchmark) {
            if (phase == 0) defaultHash = hash;
            else Require(hash == defaultHash, "explicit OFF/recreation equals original pixels");
        }
        const auto data = renderer.vsrTiming.Snapshot();
        std::printf("phase=%d requested=%s state=%d output_hash=%016llX CPU_Blt_n=%llu mean=%.4f ms\n", phase,
            g_vsrMode == llcv::vsr::Mode::On ? "on" : g_vsrMode == llcv::vsr::Mode::Off ? "off" : "default",
            static_cast<int>(renderer.vsrState),hash,data.cpuBlt.count,data.cpuBlt.Mean());
        if (benchmark) std::printf("phase=%d SERIALIZED upload+VP+readback n=%llu mean=%.4f "
            "p95_upper=%.2f p99_upper=%.2f max=%.4f ms (NOT display latency)\n",
            phase,completed.count,completed.Mean(),completed.Percentile(.95),
            completed.Percentile(.99),completed.maximum);
        if (benchmark) Require(completed.count > 100, "enough completion timing samples");
    }
    if (native) TestNativeControls(window,width,height,pixels);
    if (!benchmark) {
        TestLiveToggle(window,width,height,pixels);
        TestCenteredPixelPerfect(window,width,height,pixels);
        TestSplitResolutionRenderer(window);
    }
    DestroyWindow(window); UnregisterClassW(wc.lpszClassName,wc.hInstance);
    CoUninitialize();
    std::puts(benchmark ? "Synthetic benchmark only; S_OK/hash changes do not certify RTX activation or display latency."
                        : "PASS: policy, default/OFF pixels, six live A/B pairs, F6 repeats, coalescing, HUD, occlusion and resize.");
    return 0;
}
