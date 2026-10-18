// Included by AudioCallbackTests; exercises production paths without a device.
static void TestPlaybackMute() {
    const auto saved=g_settings;
    const auto generation=g_outputConfigurationGeneration.load();
    const int volume=g_volumePercent.load(),left=g_leftVolumePercent.load(),right=g_rightVolumePercent.load();
    const auto tracking=g_audioTrackingStartMs.load();
    const bool osd=g_audioOsdVisible.load();
    const bool fullscreen=g_fullscreen;
    g_fullscreen=false;
    g_settings.shortcuts=keys::Defaults;
    g_settings.driftCorrection=DriftCorrectionMode::Off;
    g_settings.muteWhenBackground=true;
    g_volumePercent=50;g_leftVolumePercent=80;g_rightVolumePercent=60;
    g_playbackMuted=false;g_backgroundAudioMuted=false;
    g_audioTrackingStartMs=UINT64_MAX;
    for (bool audioOnly:{false,true}) {
        g_settings.audioOnly=audioOnly;
        Require(HandleViewerShortcut(nullptr,'M',0,0) && g_playbackMuted,
                "default M mutes both video and audio-only playback");
        HandleViewerShortcut(nullptr,'M',LPARAM{1}<<30,0);
        Require(g_playbackMuted && TargetAudioVolumeGain()==0 && g_volumePercent==50,
                "held mute key does not repeat or overwrite saved volume");
        HandleViewerShortcut(nullptr,'M',0,0);
        Require(!g_playbackMuted && TargetStereoGain().left==0.4 && TargetStereoGain().right==0.3,
                "unmute restores master and separate channel gains");
    }
    g_settings.shortcuts[static_cast<size_t>(keys::Action::Mute)]=keys::Ctrl|'U';
    Require(!HandleViewerShortcut(nullptr,'M',0,0) && !HandleViewerShortcut(nullptr,'U',0,0),
            "old mute key and missing modifier are inert");
    Require(HandleViewerShortcut(nullptr,'U',0,keys::Ctrl) && g_playbackMuted,
            "custom mute combination works");
    UpdateBackgroundAudioMute(false);UpdateBackgroundAudioMute(true);
    Require(g_playbackMuted && TargetAudioVolumeGain()==0,"activation never clears manual mute");
    UpdateBackgroundAudioMute(false);SetPlaybackMuted(nullptr,false);
    Require(TargetAudioVolumeGain()==0,"manual unmute respects background auto-mute");
    UpdateBackgroundAudioMute(true);
    const std::vector<int16_t> input(960*2,12000);
    std::array<int16_t,480*2> output{};
    for (bool asio:{false,true}) {
        g_ring.Clear();
        AsioRenderState as;
        WasapiRenderState ws;
        ws.queueTargetFrames=480;ws.currentMix=TargetStereoGain();
        const auto fill=[&] {
            g_ring.Clear();g_ring.Push(input.data(),960);
            const size_t got=asio ? FillAsioPcm(&as,output.data(),480) :
                FillWasapiPcm(&ws,output.data(),480).writtenFrames;
            Require(got==480,"muted renderer keeps consuming new PCM without device restart");
        };
        fill();Require(output.back()==3600,"original channel volume before mute");
        SetPlaybackMuted(nullptr,true);fill();
        Require(output[0]>0 && output[0]<4800 && output.back()==0,
                "mute ramps through the current buffer rather than abrupt discontinuity");
        fill();Require(std::all_of(output.begin(),output.end(),[](int16_t v){return v==0;}),
                "both production output backends produce silence while muted");
        SetPlaybackMuted(nullptr,false);fill();
        Require(output[0]>0 && output[0]<4800 && output.back()==3600,
                "unmute ramps back without losing the original gain");
        fill();Require(output[0]==4800 && output[1]==3600,"gain restored exactly after ramp");
    }
    HWND hwnd=CreateWindowW(L"STATIC",L"Mute interaction fixture",WS_POPUP,
        100,100,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(hwnd!=nullptr,"mute interaction window");
    for (bool audioOnly:{false,true}) {
        g_settings.audioOnly=audioOnly;g_audioOsdVisible=true;
        g_settings.borderlessWindow=true;
        SetWindowPos(hwnd,nullptr,100,100,audioOnly?380:640,audioOnly?230:360,SWP_NOZORDER|SWP_NOACTIVATE);
        const auto box=llcv::audio_osd::RectForClient(640);
        const POINT client=audioOnly ? POINT{40,70} : POINT{box.left+38,box.top+48};
        const auto position=MAKELPARAM(client.x,client.y);
        POINT screen=client;ClientToScreen(hwnd,&screen);
        if(!audioOnly) {
            HWND child=CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,640,360,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
            Require(child && SetWindowSubclass(child,VideoHostSubclassProc,1,0),"video-host mute input fixture");
            for(bool borderless:{false,true}) for(bool full:{false,true}) {
                g_settings.borderlessWindow=borderless;g_fullscreen=full;
                Require(SendMessageW(child,WM_NCHITTEST,0,MAKELPARAM(screen.x,screen.y))==HTTRANSPARENT,
                        "video host forwards mute hit in normal/borderless/fullscreen modes");
            }
            DestroyWindow(child);g_settings.borderlessWindow=true;g_fullscreen=false;
        }
        Require(WndProc(hwnd,WM_NCHITTEST,0,MAKELPARAM(screen.x,screen.y))==HTCLIENT,
                "mute hit stays client-side in a borderless window");
        SetPlaybackMuted(nullptr,false);
        WndProc(hwnd,WM_LBUTTONDOWN,0,position);
        Require(g_muteButtonPressed && !g_audioOnlyDragPending,"speaker press captures without moving window");
        WndProc(hwnd,WM_LBUTTONUP,0,position);
        Require(g_playbackMuted && GetCapture()!=hwnd,"release on button toggles and releases capture");
        WndProc(hwnd,WM_LBUTTONDOWN,0,position);
        WndProc(hwnd,WM_LBUTTONUP,0,MAKELPARAM(5,5));
        Require(g_playbackMuted,"release outside button cancels toggle");
        WndProc(hwnd,WM_LBUTTONDOWN,0,position);
        WndProc(hwnd,WM_CANCELMODE,0,0);
        WndProc(hwnd,WM_LBUTTONUP,0,position);
        Require(g_playbackMuted && !g_muteButtonPressed,"cancelled press cannot toggle later");
        WndProc(hwnd,WM_LBUTTONDBLCLK,0,position);
        WndProc(hwnd,WM_LBUTTONUP,0,position);
        Require(!g_playbackMuted && g_volumePercent==50,"double-click never resets master volume on speaker");
        SetPlaybackMuted(nullptr,true);
        WndProc(hwnd,WM_LBUTTONDBLCLK,0,audioOnly?MAKELPARAM(100,70):MAKELPARAM(box.left+110,box.top+48));
        Require(!g_playbackMuted && g_volumePercent==100,"resetting the volume unmutes and restores unity");
        g_volumePercent=50;
    }
    DestroyWindow(hwnd);
    Require(g_outputConfigurationGeneration==generation,"mute never rebuilds capture/render output");
    g_ring.Clear();g_settings=saved;
    g_volumePercent=volume;g_leftVolumePercent=left;g_rightVolumePercent=right;
    g_audioTrackingStartMs=tracking;g_audioOsdVisible=osd;
    g_fullscreen=fullscreen;
    g_playbackMuted=false;g_backgroundAudioMuted=false;
    std::puts("Playback mute: keys, repeats, mouse cancellation, gain ramps and output isolation passed.");
}
