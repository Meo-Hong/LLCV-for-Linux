// Real viewer/settings handlers and CPU recorder, hidden native windows only.
// This translation unit replaces MessageBoxW so confirmation choices are
// deterministic without displaying UI. No capture/audio devices, GPU renderer,
// production entry point, settings profile load, or process relaunch is used.
#include <Windows.h>
static int WINAPI TestRecordingMessageBox(HWND,LPCWSTR,LPCWSTR,UINT);
#define MessageBoxW TestRecordingMessageBox
#include "../src/main.cpp"
#undef fwprintf
#undef MessageBoxW
#include "../src/audio/AsioOutput.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

#if !defined(LLCV_RECORDING) || !defined(LLCV_SETTINGS_MODE_TEST)
#error Requires LLCV_RECORDING and the isolated LLCV_SETTINGS_MODE_TEST profile.
#endif

namespace {
const char* currentCase="setup";
int confirmationAnswer=IDNO;
unsigned confirmations=0,destroyedViewers=0;
bool publishNestedClose=false;
UINT_PTR nestedCloseSerial=0;

void Require(bool value,const char* label) {
    if(!value){std::fprintf(stderr,"FAIL [%s]: %s\n",currentCase,label);std::exit(1);}
}
void Check(HRESULT hr,const char* label) {
    if(FAILED(hr)){std::fprintf(stderr,"FAIL [%s]: %s 0x%08X\n",currentCase,label,static_cast<unsigned>(hr));std::exit(1);}
}
LRESULT CALLBACK HiddenViewerProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_DESTROY)++destroyedViewers;
    return WndProc(hwnd,message,w,l);
}

void DeliverSyntheticMedia() {
    Microsoft::WRL::ComPtr<IMemAllocator> allocator;
    Check(CoCreateInstance(CLSID_MemoryAllocator,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&allocator)),"synthetic sample allocator");
    constexpr LONG frameBytes=160*90*3/2;
    ALLOCATOR_PROPERTIES requested{2,frameBytes,1,0},actual{};
    Check(allocator->SetProperties(&requested,&actual),"allocator properties");
    Check(allocator->Commit(),"allocator commit");
    for(int frame=0;frame<8;++frame){
        LONGLONG start=frame*10000000LL/30,end=(frame+1)*10000000LL/30;
        for(bool video:{true,false}){
            Microsoft::WRL::ComPtr<IMediaSample> sample;
            Check(allocator->GetBuffer(&sample,nullptr,nullptr,0),"synthetic sample");
            const LONG bytes=video?frameBytes:1600*4;
            Check(sample->SetActualDataLength(bytes),"sample length");
            Check(sample->SetTime(&start,&end),"sample timestamp");
            BYTE* data=nullptr;Check(sample->GetPointer(&data),"sample data");
            if(video)std::memset(data,128,bytes);
            else {
                auto* pcm=reinterpret_cast<short*>(data);
                for(int i=0;i<1600;++i)pcm[2*i]=pcm[2*i+1]=static_cast<short>(
                    8000*std::sin((frame*1600+i)*6.283185307179586*440/48000));
            }
            llcv::recording::recorder.Push(video,sample.Get());
        }
        Sleep(20);
    }
    Check(allocator->Decommit(),"allocator decommit");
}

void DispatchSettingsShortcut(HWND viewer,int answer) {
    confirmationAnswer=answer;
    const auto before=confirmations;
    const auto binding=g_settings.shortcuts[static_cast<size_t>(keys::Action::Settings)];
    Require(HandleViewerShortcut(viewer,binding&0xff,0,binding&0x700),"configured settings shortcut handled");
    MSG close{};
    Require(PeekMessageW(&close,viewer,WM_CLOSE,WM_CLOSE,PM_REMOVE)!=FALSE,"settings shortcut queues normal close");
    DispatchMessageW(&close);
    if(g_videoOutputThreadActive && g_deferredPresentationUi==DeferredPresentationUi::ClosePrompt){
        UINT_PTR serial=0;const auto action=TakeDeferredPresentationUi(serial);
        Require(action==DeferredPresentationUi::ClosePrompt && g_presentationUiPending,
            "exclusive close waits for output-owner acknowledgement");
        // Simulate only the render owner's acknowledgement. No renderer or GPU
        // exists in this test, but production ticket validation/handlers run.
        SendMessageW(viewer,WM_PRESENTATION_UI_READY,static_cast<WPARAM>(action),static_cast<LPARAM>(serial));
    }
    Require(confirmations==before+1,"exactly one real close confirmation");
}

void VerifyCancelled(HWND viewer,const llcv::recording::Snapshot& before,
                     const llcv::recording::Options& options) {
    const auto now=llcv::recording::recorder.GetSnapshot();
    Require(IsWindow(viewer) && !IsWindowVisible(viewer) && g_running && destroyedViewers==0,
        "No preserves hidden viewer and running playback");
    Require(now.busy==before.busy && now.phase==before.phase && now.paused==before.paused &&
        now.path==before.path && now.result==before.result && now.failure==before.failure &&
        llcv::recording::recorder.Recording(),"No preserves recording state, file, and capture admission");
    if(before.paused)Require(now.elapsedMs==before.elapsedMs,"No keeps paused elapsed time frozen");
    Require(!g_restartToSettings && !g_recordingClosePending && !g_recordingDrainPending &&
        !g_recordingClosePrompt && !llcv::recording::ControlsClosing(),"No does not arm shutdown or restart");
    Require(g_requestedSettingsTab==-1 && g_settings.volumePercent==73 && g_volumePercent==73 && g_playbackMuted &&
        llcv::recording::sessionOptions==options,"No preserves playback and recording options");
    if(g_recordingSettingsWindow){
        Require(IsWindow(g_recordingSettingsWindow),"No preserves live settings HWND");
        // Regression: the inner ClosePrompt acknowledgement cleared pending
        // and unblocked exclusive output despite the outer live dialog staying.
        Require(g_presentationBlocked.load(),"No keeps exclusive output blocked while live settings remains open");
    }
}

struct HiddenLiveSettings {
    SettingsDialogState state;
    HWND hwnd=nullptr;
    HiddenLiveSettings(HWND owner,const wchar_t* className,HINSTANCE instance) {
        state.recordingLive=true;state.activeTab=SettingsTab::Recording;
        hwnd=CreateWindowW(className,L"Hidden recording settings",WS_OVERLAPPED,
            -32000,-32000,1000,650,owner,nullptr,instance,&state);
        g_recordingSettingsWindow=hwnd;
        Require(hwnd && !IsWindowVisible(hwnd) && !state.recordingProbe && !state.inventoryCache &&
            !state.captureAudioProbeCache && !state.videoModeCache && !state.sharedProbeCache && !state.exclusiveScanRunning,
            "live settings stays hidden without any hardware/probe worker");
        Require(llcv::settings_ui::CanToggleRecordingSettings(&state),"live recording controls available");
        RefreshPresentationUiBlock();
    }
    void Close() {
        if(IsWindow(hwnd))SendMessageW(hwnd,WM_CLOSE,0,0);
        Require(!IsWindow(hwnd) && !g_recordingSettingsWindow,"closing live settings destroys only that dialog");
    }
    ~HiddenLiveSettings() {
        if(IsWindow(hwnd))Close();
        for(auto font:state.uiFonts)DeleteObject(font);
    }
};

void TestOtherUiBlocks(HWND viewer,const wchar_t* liveClass,HINSTANCE instance) {
    {
        HiddenLiveSettings live(viewer,liveClass,instance);
        g_presentationUiPending=false;
        live.Close();
        Require(!g_presentationBlocked && llcv::recording::recorder.Recording() && IsWindow(viewer),
            "closing only live settings restores output without ending playback/recording");
    }
    {
        HiddenLiveSettings live(viewer,liveClass,instance);
        g_presentationUiPending=true;
        live.Close();
        Require(g_presentationBlocked,"another pending UI request keeps exclusive output blocked");
        g_presentationUiPending=false;RefreshPresentationUiBlock();
        Require(!g_presentationBlocked,"last pending UI completion releases output");
    }
    {
        HiddenLiveSettings live(viewer,liveClass,instance);
        g_viewerHelp.SetBindings(g_settings.shortcuts);
        g_viewerHelp.Toggle(viewer,IsEnglishUi(),kAppVersionLabel,SW_HIDE,g_settings.settingsLightTheme);
        Require(g_viewerHelp.Handle() && !IsWindowVisible(g_viewerHelp.Handle()),"help fixture remains hidden");
        live.Close();
        Require(g_presentationBlocked,"existing help retains output block after live settings closes");
        g_viewerHelp.Close();RefreshPresentationUiBlock();
        Require(!g_presentationBlocked,"closing last help releases output");
    }
}

void FinishAndVerify(HWND viewer,const std::wstring& path) {
    DispatchSettingsShortcut(viewer,IDYES);
    Require(IsWindow(viewer) && g_running && g_restartToSettings && g_recordingClosePending &&
        llcv::recording::ControlsClosing() && !llcv::recording::recorder.Recording(),
        "Yes requests asynchronous finalization without immediately destroying viewer");
    const auto deadline=GetTickCount64()+15000;
    while(IsWindow(viewer)&&GetTickCount64()<deadline){
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT)continue;
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(IsWindow(viewer)&&!llcv::recording::recorder.Busy())SendMessageW(viewer,WM_TIMER,1,0);
        if(IsWindow(viewer)&&g_presentationClosing && g_videoOutputThreadActive){
            g_videoOutputThreadActive=false;PostMessageW(viewer,WM_CLOSE,0,0);
        }
        Sleep(2);
    }
    const auto saved=llcv::recording::recorder.GetSnapshot();
    Require(!IsWindow(viewer) && destroyedViewers==1 && !llcv::recording::recorder.Busy() && !g_running && g_restartToSettings,
        "saving finishes before single viewer destruction and preserves settings restart intent");
    Require(saved.phase==llcv::recording::Phase::Saved && saved.playable && SUCCEEDED(saved.result) && saved.path==path,
        "CPU recording finalizes as a verified playable file");
}
}

static int WINAPI TestRecordingMessageBox(HWND owner,LPCWSTR,LPCWSTR,UINT type) {
    Require((type&MB_TYPEMASK)==MB_YESNO && (type&MB_ICONMASK)==MB_ICONQUESTION,
        "no unexpected failure or timeout dialog");
    Require(owner && IsWindow(owner) && !IsWindowVisible(owner),"confirmation owner remains hidden");
    ++confirmations;
    if(publishNestedClose){
        publishNestedClose=false;
        Require(DeferPresentationUi(DeferredPresentationUi::ClosePrompt),"nested modal callback publishes a new close request");
        nestedCloseSerial=g_deferredPresentationSerial.load();
    }
    std::printf("confirmation [%s]: %s\n",currentCase,confirmationAnswer==IDNO?"No":"Yes");
    return confirmationAnswer;
}

int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    Require(RecordingCloseInteractive(),"test must not run with --smoke-test");
    Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM");Check(MFStartup(MF_VERSION),"MF");
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};
    Require(InitCommonControlsEx(&controls)!=FALSE,"common controls");
    wchar_t root[MAX_PATH]{},directory[MAX_PATH]{};
    Require(GetTempPathW(ARRAYSIZE(root),root)!=0 && GetTempFileNameW(root,L"lf2",0,directory)!=0,"unique temporary path");
    Require(DeleteFileW(directory)!=FALSE && CreateDirectoryW(directory,nullptr)!=FALSE,"owned temporary directory");
    g_testUserDataDirectory=directory;g_suppressSettingsSave=true;
    Require(SettingsPath()==std::wstring(directory)+L"\\settings.ini","isolated profile path");
    std::wprintf(L"Temporary regression files: %ls\n",directory);
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW viewerClass{};viewerClass.hInstance=instance;viewerClass.lpfnWndProc=HiddenViewerProc;
    viewerClass.lpszClassName=L"LLCV.Hidden.RecordingSettingsCancel.Viewer";
    Require(RegisterClassW(&viewerClass)!=0,"register hidden viewer");
    WNDCLASSW liveClass{};liveClass.hInstance=instance;liveClass.lpfnWndProc=SettingsWndProc;
    liveClass.lpszClassName=L"LLCV.Hidden.RecordingSettingsCancel.Live";
    Require(RegisterClassW(&liveClass)!=0,"register hidden live settings");
    for(int scenario=0;scenario<3;++scenario){
        currentCase=scenario==0?"active":scenario==1?"paused":"exclusive-live";
        auto& subject=llcv::recording::recorder;subject.Shutdown();destroyedViewers=0;
        g_settings={};g_settings.audioOnly=false;g_settings.preventSleep=false;
        if(scenario==2)g_settings.presentationMode=PresentationMode::ExclusiveFullscreen;
        g_settings.volumePercent=73;g_volumePercent=73;g_playbackMuted=true;
        g_fullscreen=false;g_autoFullscreen=false;g_videoHost=nullptr;g_videoOutputThreadActive=false;
        g_running=true;g_restartToSettings=false;g_windowPositionPersisted=false;
        HWND viewer=CreateWindowW(viewerClass.lpszClassName,L"Hidden settings cancellation",WS_POPUP,
            -32000,-32000,400,240,nullptr,nullptr,instance,nullptr);
        Require(viewer && !IsWindowVisible(viewer),"hidden viewer");
        llcv::video::VideoSampleFormat video{};
        video.valid=true;video.width=160;video.height=90;video.fps=30;video.stride=160;
        video.format=llcv::settings::VideoPixelFormat::Nv12;
        llcv::capture_audio::Format audio{};
        audio.channels=2;audio.containerBits=audio.validBits=16;audio.blockAlign=4;
        audio.path=llcv::capture_audio::Path::Direct16BitStereo;
        llcv::recording::Options options;options.encoder=L"cpu";
        options.rateControl=llcv::recording::RateControl::Bitrate;options.videoMbps=2;options.folder=directory;
        llcv::recording::sessionOptions=options;
        const auto path=std::wstring(directory)+L"\\recording-"+std::to_wstring(scenario)+L".mp4";
        subject.Configure(video,audio,{},false,options);Require(subject.Start(path),"start synthetic CPU recording");
        const auto deadline=GetTickCount64()+10000;
        while(subject.Busy()&&!subject.Recording()&&GetTickCount64()<deadline)Sleep(2);
        Require(subject.Recording(),"CPU encoder ready");DeliverSyntheticMedia();
        if(scenario==1)Require(subject.SetPaused(true),"pause before F2");
        std::unique_ptr<HiddenLiveSettings> live;
        if(scenario==2){
            g_recordingCapabilities=llcv::recording::Capabilities{};
            (*g_recordingCapabilities)[0]={true,true,{{L"Fixture",L"test"}},{true,true},{true,true}};
            live=std::make_unique<HiddenLiveSettings>(viewer,liveClass.lpszClassName,instance);
            g_videoOutputThreadActive=true;g_presentationUiPending=true;g_presentationBlocked=true;
        }
        const auto before=subject.GetSnapshot();
        g_requestedSettingsTab=static_cast<int>(SettingsTab::GuideDiagnostics);
        DispatchSettingsShortcut(viewer,IDNO);VerifyCancelled(viewer,before,options);
        if(scenario==2){
            DispatchSettingsShortcut(viewer,IDNO);VerifyCancelled(viewer,before,options);
            g_settings.shortcuts[static_cast<size_t>(keys::Action::Settings)]=keys::Ctrl|'K';
            Require(!HandleViewerShortcut(viewer,VK_F2,0,0),"old F2 stops matching after rebinding");
            DispatchSettingsShortcut(viewer,IDNO);VerifyCancelled(viewer,before,options);
            const auto questions=confirmations;MSG repeat{};
            Require(HandleViewerShortcut(viewer,'K',LPARAM{1}<<30,keys::Ctrl) &&
                !PeekMessageW(&repeat,viewer,WM_CLOSE,WM_CLOSE,PM_REMOVE) && confirmations==questions,
                "held remapped key cannot repeat close or confirmation");
            // The first acknowledgement is still on the stack when a nested
            // modal dispatch publishes another ticket. Its completion must not
            // clear the newer request's pending flag or consume its reply.
            publishNestedClose=true;const auto beforeNested=confirmations;
            DispatchSettingsShortcut(viewer,IDNO);
            Require(nestedCloseSerial!=0 && g_presentationUiPending && g_presentationBlocked &&
                g_deferredPresentationUi==DeferredPresentationUi::ClosePrompt &&
                g_deferredPresentationSerial==nestedCloseSerial,
                "older acknowledgement cannot clear a newer nested close ticket");
            UINT_PTR followupSerial=0;const auto followupAction=TakeDeferredPresentationUi(followupSerial);
            Require(followupAction==DeferredPresentationUi::ClosePrompt && followupSerial==nestedCloseSerial,
                "newer nested close ticket remains available to its output owner");
            SendMessageW(viewer,WM_PRESENTATION_UI_READY,static_cast<WPARAM>(followupAction),static_cast<LPARAM>(followupSerial));
            Require(confirmations==beforeNested+2 && !g_presentationUiPending &&
                g_deferredPresentationUi==DeferredPresentationUi::None && g_presentationBlocked,
                "newer close acknowledgement completes without dropping the live-window block");
            VerifyCancelled(viewer,before,options);
            live->Close();live.reset();
            Require(!g_presentationBlocked,"closing cancelled live dialog releases output block");
            TestOtherUiBlocks(viewer,liveClass.lpszClassName,instance);
            live=std::make_unique<HiddenLiveSettings>(viewer,liveClass.lpszClassName,instance);
        }
        FinishAndVerify(viewer,path);
        if(live)Require(!IsWindow(live->hwnd) && !g_recordingSettingsWindow,"owner finalization destroys live dialog");
        live.reset();subject.Shutdown();
        Require(DeleteFileW(path.c_str())!=FALSE,"remove only the recording created by this case");
        MSG quit{};while(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
        g_videoHost=nullptr;g_recordingCapabilities.reset();
        std::printf("PASS [%s]: cancellation preserves state; confirmation safely saves and closes\n",currentCase);
    }
    llcv::recording::SetControlsClosing(false);
    Require(UnregisterClassW(liveClass.lpszClassName,instance)!=FALSE &&
        UnregisterClassW(viewerClass.lpszClassName,instance)!=FALSE,"release hidden window classes");
    Require(RemoveDirectoryW(directory)!=FALSE,"remove empty owned temporary directory");
    g_testUserDataDirectory.clear();MFShutdown();CoUninitialize();
    std::puts("PASS: active/paused/exclusive-live cancellation, repeated/remapped keys, other UI blockers, verified saved files.");
    return 0;
}
