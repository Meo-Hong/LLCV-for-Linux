// Manual opt-in only. Opens one visible synthetic output, owns no capture/audio
// devices and never changes display modes or the user's saved settings.
struct ExclusiveRendererIntent {
    bool fullscreen = true;
    bool active = true;
    bool blocked = false;
};
struct ExclusiveRendererWindow {
    HWND root = nullptr;
    HWND host = nullptr;
    RECT monitor{};
};
static constexpr UINT kExclusiveRendererIntentMessage = WM_APP + 0x149;

static LRESULT CALLBACK ExclusiveRendererWindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto* state = reinterpret_cast<ExclusiveRendererWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<ExclusiveRendererWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, message, w, l);
    if (message == WM_CREATE) {
        state->root = hwnd;
        state->host = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_BLACKRECT,
            0, 0, state->monitor.right - state->monitor.left,
            state->monitor.bottom - state->monitor.top, hwnd, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        return state->host ? 0 : -1;
    }
    if (message == WM_SIZE) {
        if (state->host) MoveWindow(state->host, 0, 0, LOWORD(l), HIWORD(l), FALSE);
        if (g_presentationMinimized.exchange(w == SIZE_MINIMIZED) != (w == SIZE_MINIMIZED))
            NotifyPresentationChange();
        g_outputConfigurationGeneration.fetch_add(1, std::memory_order_release);
        return 0;
    }
    if (message == WM_ACTIVATE) {
        if (g_presentationActive.exchange(LOWORD(w) != WA_INACTIVE) != (LOWORD(w) != WA_INACTIVE))
            NotifyPresentationChange();
        return 0;
    }
    if (message == WM_DISPLAYCHANGE) {
        NotifyPresentationChange();
        return 0;
    }
    if (message == kExclusiveRendererIntentMessage) {
        const auto& intent = *reinterpret_cast<const ExclusiveRendererIntent*>(l);
        g_fullscreen.store(intent.fullscreen, std::memory_order_release);
        const auto& monitor = state->monitor;
        const int width = monitor.right - monitor.left, height = monitor.bottom - monitor.top;
        SetWindowPos(hwnd, HWND_TOP, monitor.left + (intent.fullscreen ? 0 : 40),
            monitor.top + (intent.fullscreen ? 0 : 40),
            intent.fullscreen ? width : (std::min)(width - 80, 960),
            intent.fullscreen ? height : (std::min)(height - 80, 540), SWP_NOACTIVATE);
        // Apply the synthetic focus/modal intent after synchronous size/focus
        // notifications, as the real UI publishes intent after its operation.
        g_presentationActive.store(intent.active, std::memory_order_release);
        g_presentationMinimized.store(false, std::memory_order_release);
        g_presentationBlocked.store(intent.blocked, std::memory_order_release);
        NotifyPresentationChange();
        return 1;
    }
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, w, l);
}

static int RunExclusiveRendererRegression() {
    const auto savedSettings = g_settings;
    const bool savedSuppress = g_suppressSettingsSave;
    const bool savedFullscreen = g_fullscreen.load();
    const bool savedOsd = g_osdVisible.load();
    const bool savedAudioOsd = g_audioOsdVisible.load();
    const auto savedHud = g_volumeHudUntilMs.load();
    const auto savedVsr = g_vsrMode.load();
    const auto savedHost = g_videoHost;
    const auto savedRevision = g_presentationRevision.load();
    const auto savedOutputGeneration = g_outputConfigurationGeneration.load();
    const bool savedActive = g_presentationActive.load();
    const bool savedMinimized = g_presentationMinimized.load();
    const bool savedBlocked = g_presentationBlocked.load();
    const bool savedClosing = g_presentationClosing.load();
    g_settings = {};
    g_settings.preventSleep = false;
    g_settings.checkForUpdates = false;
    g_settings.pixelPerfect = false;
    g_settings.presentationMode = PresentationMode::ExclusiveFullscreen;
    g_suppressSettingsSave = true;
    g_fullscreen = true;
    g_osdVisible = false;
    g_audioOsdVisible = false;
    g_volumeHudUntilMs = 0;
    g_vsrMode = llcv::vsr::Mode::Off;
    g_presentationClosing = false;
    g_presentationBlocked = false;
    g_presentationMinimized = false;

    HANDLE stopWatchdog = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Require(stopWatchdog && ready, "exclusive renderer synchronization events");
    std::thread watchdog([&] {
        if (WaitForSingleObject(stopWatchdog, 30000) == WAIT_TIMEOUT) {
            std::fprintf(stderr, "FAIL: manual exclusive renderer regression exceeded 30 seconds.\n");
            std::fflush(stderr);
            // Process teardown lets Windows reclaim fullscreen ownership even
            // when a driver call itself has stopped responding.
            TerminateProcess(GetCurrentProcess(), 2);
        }
    });
    ExclusiveRendererWindow window;
    std::thread ui([&] {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        MONITORINFO monitor{sizeof(monitor)};
        Require(GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) != FALSE,
                "exclusive renderer primary monitor");
        window.monitor = monitor.rcMonitor;
        WNDCLASSW wc{};
        wc.lpfnWndProc = ExclusiveRendererWindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LLCV_MANUAL_EXCLUSIVE_RENDERER";
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        Require(RegisterClassW(&wc) != 0, "exclusive renderer class");
        HWND root = CreateWindowExW(0, wc.lpszClassName,
            L"LLCV manual exclusive regression — synthetic video", WS_POPUP,
            monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            nullptr, nullptr, wc.hInstance, &window);
        Require(root && window.host, "exclusive renderer visible root and hidden child");
        ShowWindow(root, SW_SHOWNORMAL);
        SetForegroundWindow(root);
        SetActiveWindow(root);
        UpdateWindow(root);
        SetEvent(ready);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (IsWindow(root)) DestroyWindow(root);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
    });
    Require(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0, "exclusive renderer UI ready");
    g_videoHost = window.host;
    DirectD3D11Renderer renderer;
    unsigned phases = 0, transitions = 0, exactComparisons = 0;
    bool unavailable = false;
    for (bool hdr : {false, true}) {
        if (hdr && llcv::hdr::QueryDisplay(window.root).hdr != 1) {
            std::puts("SKIP: HDR subcase needs an active HDR output; Windows HDR is not changed by this test.");
            continue;
        }
        const ExclusiveRendererIntent initial{};
        Require(SendMessageW(window.root, kExclusiveRendererIntentMessage, 0,
                    reinterpret_cast<LPARAM>(&initial)) == 1, "exclusive renderer initial UI intent");
        constexpr int width = 640, height = 360;
        const auto format = hdr ? VideoPixelFormat::P010 : VideoPixelFormat::Nv12;
        const HRESULT initialized = renderer.initialize(window.host, width, height, 60, format, hdr);
        if (initialized == DXGI_ERROR_UNSUPPORTED || initialized == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ||
            initialized == DXGI_ERROR_SDK_COMPONENT_MISSING) {
            std::printf("SKIP: %s renderer/output unavailable, HRESULT=0x%08X.\n",
                hdr ? "HDR" : "SDR", static_cast<unsigned>(initialized));
            if (!hdr) unavailable = true;
            renderer.reset();
            if (unavailable) break;
            continue;
        }
        Check(initialized, "manual exclusive renderer initialize");
        Check(renderer.resizeOutput(window.host), "manual exclusive renderer initial enter");
        BOOL actual = FALSE;
        Check(renderer.swapChain->GetFullscreenState(&actual, nullptr), "manual exclusive initial query");
        if (!actual) {
            std::printf("SKIP: %s exclusive ownership unavailable; normal flip fallback is not an exclusive pass.\n",
                hdr ? "HDR" : "SDR");
            if (!hdr) unavailable = true;
            renderer.reset();
            if (unavailable) break;
            continue;
        }
        ++phases;
        auto* const originalDevice = renderer.device;
        const auto originalUploads = std::to_array(renderer.nv12Textures);
        auto* const originalOsdTexture = renderer.osdOverlayTexture;
        const size_t inputPixels = static_cast<size_t>(width) * height;
        std::vector<BYTE> input(hdr ? inputPixels * 3 : inputPixels * 3 / 2, 128);
        if (hdr) {
            auto* words = reinterpret_cast<WORD*>(input.data());
            for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel)
                words[pixel] = static_cast<WORD>((96 + pixel % 700) << 6);
            for (size_t pixel = static_cast<size_t>(width) * height; pixel < input.size() / sizeof(WORD); ++pixel)
                words[pixel] = 512 << 6;
        } else {
            for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel)
                input[pixel] = static_cast<BYTE>(32 + pixel % 190);
        }
        const UINT stride = width * (hdr ? 2u : 1u);
        auto checkOutput = [&] {
            Require(renderer.device == originalDevice && renderer.osdOverlayTexture == originalOsdTexture,
                    "exclusive transitions retain device and OSD cache");
            Require(std::to_array(renderer.nv12Textures) == originalUploads,
                    "exclusive transitions retain the original upload ring");
            if (hdr) {
                Require(renderer.hdrOutput && renderer.backBufferFormat == DXGI_FORMAT_R10G10B10A2_UNORM &&
                        !renderer.skipRedundantBorderClear && renderer.videoContext1,
                        "HDR transitions retain 10-bit output and conservative clear");
                DXGI_COLOR_SPACE_TYPE outputSpace{};
                renderer.videoContext1->VideoProcessorGetOutputColorSpace1(renderer.processor, &outputSpace);
                Require(outputSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,
                        "HDR Video Processor remains BT.2020 PQ after transitions");
                Microsoft::WRL::ComPtr<IDXGISwapChain3> chain;
                Check(renderer.swapChain->QueryInterface(IID_PPV_ARGS(&chain)), "HDR swapchain interface");
                UINT support = 0;
                Check(chain->CheckColorSpaceSupport(outputSpace, &support), "HDR output color-space support");
                Require((support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0,
                        "HDR swapchain still supports its configured output color space");
            }
            renderer.upload(input.data(), stride);
            g_osdVisible = true;
            ++g_overlayGeneration;
            const HRESULT present = renderer.presentUploaded();
            Check(present, "exclusive synthetic video and OSD Present");
            if (present != S_OK)
                std::printf("Present status 0x%08X (not counted as a displayed frame).\n",
                    static_cast<unsigned>(present));
        };
        checkOutput();
        if (!hdr) {
            auto draw = [&](bool osd, bool omitClear) {
                renderer.context->OMSetRenderTargets(0, nullptr, nullptr);
                const float black[4]{0, 0, 0, 1};
                if (!omitClear) renderer.context->ClearRenderTargetView(renderer.backBufferRenderTarget, black);
                renderer.upload(input.data(), stride);
                D3D11_VIDEO_PROCESSOR_STREAM stream{};
                stream.Enable = TRUE;
                stream.pInputSurface = renderer.inputViews[renderer.activeUploadSurface];
                Check(renderer.processVideo(stream), "exclusive BGRA comparison video");
                g_osdVisible = osd;
                g_volumeHudUntilMs = 0;
                ++g_overlayGeneration;
                Check(renderer.drawOverlayQuads(), "exclusive BGRA comparison OSD");
                renderer.context->OMSetRenderTargets(0, nullptr, nullptr);
                return OutputPixels(renderer);
            };
            const auto video = draw(false, false);
            Require(video == draw(false, true), "exclusive full-frame BGRA matches original clear");
            const auto overlay = draw(true, false);
            Require(overlay != video, "exclusive OSD actually changes output pixels");
            Require(overlay == draw(true, true), "exclusive OSD pixels match original clear");
            exactComparisons += 2;
        }
        for (const auto& intent : {ExclusiveRendererIntent{false, true, false},
                 ExclusiveRendererIntent{true, true, false}, ExclusiveRendererIntent{true, true, true},
                 ExclusiveRendererIntent{true, true, false}, ExclusiveRendererIntent{true, false, false},
                 ExclusiveRendererIntent{true, true, false}}) {
            Require(SendMessageW(window.root, kExclusiveRendererIntentMessage, 0,
                        reinterpret_cast<LPARAM>(&intent)) == 1, "exclusive renderer transition UI intent");
            Check(renderer.resizeOutput(window.host), "exclusive renderer transition resize");
            Check(renderer.swapChain->GetFullscreenState(&actual, nullptr), "exclusive renderer transition query");
            Require((actual != FALSE) == (intent.fullscreen && intent.active && !intent.blocked),
                    "actual exclusive state follows fullscreen, settings and focus intent");
            Require(renderer.exclusiveActive == (actual != FALSE), "renderer tracks actual fullscreen state");
            ++transitions;
            checkOutput();
        }
#ifdef LLCV_VSR_FEATURE
        for (auto mode : {llcv::vsr::Mode::On, llcv::vsr::Mode::Off}) {
            g_vsrMode = mode;
            checkOutput();
            std::printf("VSR %s %s: %ls (request classification; not proof of RTX visual activation).\n",
                hdr ? "HDR" : "SDR", mode == llcv::vsr::Mode::On ? "ON" : "OFF",
                llcv::vsr::StateName(renderer.vsrState));
            Require(renderer.vsrState != llcv::vsr::State::Unknown,
                    "VSR transition does not leave an unknown renderer state");
        }
#endif
        // Hold a second reference so reset's fullscreen release can be queried
        // before closing the UI window, independent from the renderer fields.
        Microsoft::WRL::ComPtr<IDXGISwapChain1> retained = renderer.swapChain;
        renderer.reset();
        Check(retained->GetFullscreenState(&actual, nullptr), "exclusive reset state query");
        Require(!actual && IsWindow(window.root), "renderer reset releases exclusive before UI destruction");
        retained.Reset();
        g_vsrMode = llcv::vsr::Mode::Off;
    }
    renderer.reset();
    Require(PostMessageW(window.root, WM_CLOSE, 0, 0) != FALSE, "exclusive renderer UI close after reset");
    ui.join();
    SetEvent(stopWatchdog);
    watchdog.join();
    CloseHandle(ready);
    CloseHandle(stopWatchdog);
    g_settings = savedSettings;
    g_suppressSettingsSave = savedSuppress;
    g_fullscreen = savedFullscreen;
    g_osdVisible = savedOsd;
    g_audioOsdVisible = savedAudioOsd;
    g_volumeHudUntilMs = savedHud;
    g_vsrMode = savedVsr;
    g_videoHost = savedHost;
    g_presentationRevision = savedRevision;
    g_outputConfigurationGeneration = savedOutputGeneration;
    g_presentationActive = savedActive;
    g_presentationMinimized = savedMinimized;
    g_presentationBlocked = savedBlocked;
    g_presentationClosing = savedClosing;
    std::printf("%s: manual exclusive renderer phases=%u transitions=%u exact BGRA comparisons=%u; "
        "no capture/audio devices or display-mode changes.\n", unavailable ? "SKIP" : "PASS",
        phases, transitions, exactComparisons);
    return unavailable ? 77 : 0;
}
