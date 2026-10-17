// Hidden native windows and a simulated render owner. No GPU, capture, audio,
// fullscreen calls, monitor-mode changes, or settings-file writes.
struct ExclusiveViewerFixture {
    unsigned destroyed = 0;
    unsigned synchronousReplies = 0;
    unsigned deferredUiReplies = 0;
};
static ExclusiveViewerFixture* g_exclusiveViewerFixture = nullptr;
static constexpr UINT kExclusiveReleaseMessage = WM_APP + 0x147;

static LRESULT CALLBACK ExclusiveViewerTestProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == kExclusiveReleaseMessage) {
        ++g_exclusiveViewerFixture->synchronousReplies;
        return 0x147;
    }
    if (message == WM_DESTROY) ++g_exclusiveViewerFixture->destroyed;
    if (message == WM_PRESENTATION_UI_READY) {
        ++g_exclusiveViewerFixture->deferredUiReplies;
#ifdef LLCV_RECORDING
        // Exercise acknowledgement consumption without creating a visible,
        // nested recording dialog in this hidden-window protocol fixture.
        if (w == static_cast<WPARAM>(DeferredPresentationUi::RecordingSettings))
            return WndProc(hwnd, message, static_cast<WPARAM>(DeferredPresentationUi::None), l);
#endif
    }
    return WndProc(hwnd, message, w, l);
}

static void TestExclusiveViewerLifecycle() {
    const auto savedSettings = g_settings;
    const bool savedSuppress = g_suppressSettingsSave;
    const bool savedRunning = g_running.load();
    const bool savedFullscreen = g_fullscreen.load();
    const bool savedOutputActive = g_videoOutputThreadActive.load();
    const auto savedRevision = g_presentationRevision.load();
    const bool savedActive = g_presentationActive.load();
    const bool savedMinimized = g_presentationMinimized.load();
    const bool savedBlocked = g_presentationBlocked.load();
    const bool savedClosing = g_presentationClosing.load();
    const auto savedDeferredUi = g_deferredPresentationUi.load();
    const auto savedDeferredSerial = g_deferredPresentationSerial.load();
    const auto savedUiSerial = g_presentationUiSerial;
    const bool savedUiPending = g_presentationUiPending;
    const MSG savedHelpMessage = g_deferredHelpMessage;
    const unsigned savedHelpModifiers = g_deferredHelpModifiers;
    const bool savedRestart = g_restartToSettings.load();
    const auto savedHost = g_videoHost;
    g_suppressSettingsSave = true;

    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = ExclusiveViewerTestProc;
    wc.lpszClassName = L"LLCV_HIDDEN_EXCLUSIVE_LIFECYCLE";
    Require(RegisterClassW(&wc) != 0, "register hidden exclusive lifecycle class");

    for (bool quitMessage : {false, true}) {
        ExclusiveViewerFixture fixture;
        g_exclusiveViewerFixture = &fixture;
        g_settings = {};
        g_settings.preventSleep = false;
        g_settings.presentationMode = PresentationMode::ExclusiveFullscreen;
        g_fullscreen = false;
        g_running = true;
        g_restartToSettings = false;
        g_videoHost = nullptr;
        g_videoOutputThreadActive = false;
        HWND hwnd = CreateWindowW(wc.lpszClassName, L"Hidden exclusive lifecycle", WS_POPUP,
            -10000, -10000, 400, 240, nullptr, nullptr, wc.hInstance, nullptr);
        Require(hwnd != nullptr, "create hidden exclusive viewer");
        Require(g_videoHost && !IsWindowVisible(g_videoHost) && PresentationWindow(g_videoHost) == hwnd,
                "exclusive binds to root and hides the old child output surface");

        const auto initialRevision = g_presentationRevision.load();
        SendMessageW(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        Require(g_presentationActive.load() && g_presentationRevision.load() > initialRevision,
                "focus publishes exclusive intent");
        const auto focusedRevision = g_presentationRevision.load();
        SendMessageW(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        Require(g_presentationRevision.load() == focusedRevision,
                "duplicate focus does not cause frame-by-frame reconfiguration");
        SendMessageW(hwnd, WM_ACTIVATE, WA_INACTIVE, 0);
        Require(!g_presentationActive.load() && g_presentationRevision.load() > focusedRevision,
                "Alt+Tab publishes exclusive suspension without calling DXGI on the UI thread");
        const auto inactiveRevision = g_presentationRevision.load();
        SendMessageW(hwnd, WM_SIZE, SIZE_MINIMIZED, 0);
        Require(g_presentationMinimized.load() && g_presentationRevision.load() > inactiveRevision,
                "minimize publishes exclusive suspension even without capture samples");
        const auto minimizedRevision = g_presentationRevision.load();
        SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(400, 240));
        Require(!g_presentationMinimized.load() && g_presentationRevision.load() > minimizedRevision,
                "restore publishes a new exclusive intent");

        g_videoOutputThreadActive = true;
        Require(!g_viewerHelp.Handle(), "deferred UI fixture begins without a help window");
        g_viewerHelp.SetBindings(g_settings.shortcuts);
        g_viewerHelp.Toggle(hwnd, IsEnglishUi(), kAppVersionLabel, SW_HIDE,
            g_settings.settingsLightTheme);
        Require(g_viewerHelp.Handle() && !IsWindowVisible(g_viewerHelp.Handle()),
                "modeless help fixture is created without a visible window");
        RefreshPresentationUiBlock();
        Require(g_presentationBlocked.load(), "existing modeless help blocks exclusive entry");
        SendMessageW(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        RefreshPresentationUiBlock();
        Require(g_presentationActive.load() && g_presentationBlocked.load(),
                "reactivating viewer cannot enter exclusive while modeless help exists");
        g_viewerHelp.Close();
        RefreshPresentationUiBlock();
        Require(!g_presentationBlocked.load(), "closing modeless help releases its exclusive block");
        MSG helpRequest{};
        helpRequest.hwnd = hwnd;
        helpRequest.message = WM_KEYDOWN;
        helpRequest.wParam = g_settings.shortcuts[static_cast<size_t>(keys::Action::Help)] & 0xffu;
        helpRequest.lParam = 1;
        helpRequest.pt = {17, 23};
        const auto beforeHelp = g_presentationRevision.load();
        Require(DeferPresentationUi(DeferredPresentationUi::Help, &helpRequest, keys::Ctrl),
                "help queues while render owner is alive even when cached exclusive state is false");
        Require(g_presentationUiPending && g_presentationBlocked.load() &&
                g_deferredPresentationUi.load() == DeferredPresentationUi::Help &&
                g_presentationRevision.load() > beforeHelp && !g_viewerHelp.Handle(),
                "queued help blocks exclusive entry and cannot open before render acknowledgement");
        Require(g_deferredHelpMessage.hwnd == hwnd && g_deferredHelpMessage.pt.x == 17 &&
                g_deferredHelpModifiers == keys::Ctrl,
                "deferred help retains original message and modifier snapshot");
        const auto pendingRevision = g_presentationRevision.load();
        MSG repeatedHelp = helpRequest;
        repeatedHelp.lParam |= LPARAM{1} << 30;
        Require(DeferPresentationUi(DeferredPresentationUi::RecordingSettings, &repeatedHelp, keys::Alt) &&
                g_deferredPresentationUi.load() == DeferredPresentationUi::Help &&
                g_presentationRevision.load() == pendingRevision &&
                g_deferredHelpModifiers == keys::Ctrl,
                "repeat or competing UI request preserves the first pending action without revision churn");

        const auto helpSerial = g_deferredPresentationSerial.load();
        Require(DeferPresentationUi(DeferredPresentationUi::ClosePrompt) &&
                g_deferredPresentationSerial.load() > helpSerial &&
                g_deferredPresentationUi.load() == DeferredPresentationUi::ClosePrompt,
                "close prompt supersedes queued help with a fresh acknowledgement ticket");
        const auto closeSerial = g_deferredPresentationSerial.load();
        SendMessageW(hwnd, WM_PRESENTATION_UI_READY,
            static_cast<WPARAM>(DeferredPresentationUi::Help), static_cast<LPARAM>(helpSerial));
        Require(g_presentationUiPending && g_presentationBlocked.load() &&
                g_deferredPresentationUi.load() == DeferredPresentationUi::ClosePrompt &&
                !g_viewerHelp.Handle(),
                "stale help acknowledgement cannot authorize or clear newer close prompt");

        // A matching render acknowledgement after close intent must consume
        // the request without opening any visible modal/modeless window.
        g_presentationClosing = true;
        g_deferredPresentationUi = DeferredPresentationUi::None;
        SendMessageW(hwnd, WM_PRESENTATION_UI_READY,
            static_cast<WPARAM>(DeferredPresentationUi::ClosePrompt), static_cast<LPARAM>(closeSerial));
        Require(!g_presentationUiPending && !g_presentationBlocked.load() && !g_viewerHelp.Handle(),
                "close intent prevents a queued help acknowledgement from reopening UI");
        g_presentationClosing = false;

        Require(DeferPresentationUi(DeferredPresentationUi::RecordingSettings),
                "settings queue before any new capture sample");
        Require(g_presentationUiPending && g_presentationBlocked.load() &&
                g_deferredPresentationUi.load() == DeferredPresentationUi::RecordingSettings,
                "settings wait for render completion rather than opening synchronously");
        std::atomic<bool> acknowledgementPosted{false};
        std::thread noInputOwner([&] {
            // Simulated render-owner completion: the real GPU transition is
            // separately tested. The fixture suppresses actual recording
            // dialog creation so acknowledgement stays entirely hidden.
            UINT_PTR serial = 0;
            const auto action = TakeDeferredPresentationUi(serial);
            acknowledgementPosted = action == DeferredPresentationUi::RecordingSettings &&
                PostMessageW(hwnd, WM_PRESENTATION_UI_READY, static_cast<WPARAM>(action),
                    static_cast<LPARAM>(serial)) != FALSE;
        });
        const auto acknowledgeDeadline = GetTickCount64() + 2000;
        while (g_presentationUiPending && GetTickCount64() < acknowledgeDeadline) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
            MSG ready{};
            while (PeekMessageW(&ready, nullptr, 0, 0, PM_REMOVE)) {
                if (ready.message == WM_QUIT) continue;
                TranslateMessage(&ready);
                DispatchMessageW(&ready);
            }
        }
        noInputOwner.join();
        Require(acknowledgementPosted && !g_presentationUiPending && !g_presentationBlocked.load(),
                "render acknowledgement completes settings handoff without capture samples or blocking the UI");
        Require(fixture.deferredUiReplies == 3,
                "stale help, cancelled close prompt and completed settings each deliver one acknowledgement");

        // Superseding requests may race render-owner consumption, but their
        // action and ticket must always belong to the same publication.
        const auto firstTicket = g_presentationUiSerial;
        std::atomic<bool> publicationsDone{false}, requestPairsMatch{true};
        std::atomic<unsigned> consumedRequests{0};
        std::thread requestConsumer([&] {
            while (!publicationsDone.load(std::memory_order_acquire) ||
                   g_deferredPresentationUi.load() != DeferredPresentationUi::None) {
                UINT_PTR ticket = 0;
                const auto action = TakeDeferredPresentationUi(ticket);
                if (action == DeferredPresentationUi::None) { std::this_thread::yield(); continue; }
                const auto expected = (ticket - firstTicket) % 2
                    ? DeferredPresentationUi::Help : DeferredPresentationUi::ClosePrompt;
                if (action != expected) requestPairsMatch = false;
                ++consumedRequests;
            }
        });
        for (unsigned publication = 0; publication < 2000; ++publication) {
            g_presentationUiPending = false; // simulate cancellation/supersession on the UI owner
            Require(DeferPresentationUi(publication % 2
                ? DeferredPresentationUi::ClosePrompt : DeferredPresentationUi::Help),
                "publish superseding UI request in pair stress");
        }
        publicationsDone.store(true, std::memory_order_release);
        requestConsumer.join();
        Require(requestPairsMatch && consumedRequests != 0,
                "concurrent consumption never pairs an old UI action with a newer request ticket");
        g_presentationUiPending = false;
        RefreshPresentationUiBlock();

        Require(DeferPresentationUi(DeferredPresentationUi::Help, &helpRequest),
                "another help request can queue after prior handoff");
        std::atomic<bool> closingObserved{false}, ownerAlive{false}, replyReceived{false}, playbackStopped{false};
        std::thread output([&] {
            const auto deadline = GetTickCount64() + 2000;
            while (!g_presentationClosing.load(std::memory_order_acquire) && GetTickCount64() < deadline)
                Sleep(1);
            closingObserved = g_presentationClosing.load(std::memory_order_acquire);
            playbackStopped = !g_running.load(std::memory_order_acquire);
            ownerAlive = IsWindow(hwnd) != FALSE;
            DWORD_PTR reply = 0;
            replyReceived = SendMessageTimeoutW(hwnd, kExclusiveReleaseMessage, 0, 0,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &reply) != 0 && reply == 0x147;
            g_videoOutputThreadActive.store(false, std::memory_order_release);
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        });
        if (quitMessage) PostQuitMessage(0);
        else Require(PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE, "queue exclusive viewer close");
        RunViewerMessageLoop(hwnd);
        output.join();
        Require(closingObserved && ownerAlive && replyReceived && fixture.synchronousReplies == 1,
                "close and WM_QUIT keep UI pumping until the render owner releases exclusive output");
        Require(playbackStopped, "audio/capture stop before output teardown without stopping the UI pump");
        Require(!IsWindow(hwnd) && fixture.destroyed == 1 && !g_videoOutputThreadActive.load(),
                "render-owner completion destroys hidden viewer exactly once");
        Require(!g_presentationUiPending &&
                g_deferredPresentationUi.load() == DeferredPresentationUi::None && !g_viewerHelp.Handle(),
                "close cancels a pending modal action and cannot reopen help after teardown");
        MSG pending{};
        while (PeekMessageW(&pending, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
        g_videoHost = nullptr;
    }
    g_exclusiveViewerFixture = nullptr;
    Require(UnregisterClassW(wc.lpszClassName, wc.hInstance) != FALSE, "release exclusive lifecycle class");
    g_settings = savedSettings;
    g_suppressSettingsSave = savedSuppress;
    g_running = savedRunning;
    g_fullscreen = savedFullscreen;
    g_videoOutputThreadActive = savedOutputActive;
    g_presentationRevision = savedRevision;
    g_presentationActive = savedActive;
    g_presentationMinimized = savedMinimized;
    g_presentationBlocked = savedBlocked;
    g_presentationClosing = savedClosing;
    g_deferredPresentationUi = savedDeferredUi;
    g_deferredPresentationSerial = savedDeferredSerial;
    g_presentationUiSerial = savedUiSerial;
    g_presentationUiPending = savedUiPending;
    g_deferredHelpMessage = savedHelpMessage;
    g_deferredHelpModifiers = savedHelpModifiers;
    g_restartToSettings = savedRestart;
    g_videoHost = savedHost;
    std::puts("PASS: exclusive focus/minimize, deferred help/settings, no-input acknowledgement/close, WM_QUIT and synchronous owner teardown.");
}
