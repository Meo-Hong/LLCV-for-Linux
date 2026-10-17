// Exercise the real UI boundary without a capture device, encoder or file IO.
#include "../src/recording/RecordingControls.cpp"
#include <cstdio>
#include <cstdlib>
namespace llcv::recording {
Snapshot fixtureSnapshot;
unsigned stopCalls=0,startCalls=0;
Recorder recorder;
Recorder::~Recorder(){if(wake_)CloseHandle(wake_);}
Snapshot Recorder::GetSnapshot() const{return fixtureSnapshot;}
bool Recorder::Start(const std::wstring&,const Options* options){++startCalls;return options!=nullptr;}
void Recorder::Stop() noexcept{++stopCalls;}
std::wstring DefaultFolder(){return L"C:\\recordings";}
}
using namespace llcv::recording;
static void Check(bool ok,const char* message){if(!ok){fprintf(stderr,"FAIL %s\n",message);std::abort();}}
static LRESULT CALLBACK Host(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_CLOSE){DestroyWindow(h);return 0;}
    return DefWindowProcW(h,m,w,l);
}
#ifdef LLCV_RECORDING_INDICATOR_PREVIEW
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW c{};c.lpfnWndProc=Host;c.hInstance=instance;c.lpszClassName=L"LLCVIndicatorPreview";
    c.hbrBackground=GetSysColorBrush(COLOR_WINDOW);RegisterClassW(&c);
    HWND host=CreateWindowW(c.lpszClassName,L"LLCV · 녹화 표시 미리보기 (F1~F6)",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        CW_USEDEFAULT,CW_USEDEFAULT,960,600,nullptr,nullptr,instance,nullptr);
    AttachRecordingIndicator(host,host,false,false);
    fixtureSnapshot.phase=Phase::Recording;fixtureSnapshot.busy=true;fixtureSnapshot.elapsedMs=154000;
    fixtureSnapshot.path=L"C:\\Recordings\\LLCV-preview.mp4";
    UpdateRecordingIndicator(host,fixtureSnapshot);
    MSG msg{};
    while(IsWindow(host)&&GetMessageW(&msg,nullptr,0,0)>0){
        if(msg.message==WM_KEYDOWN&&msg.wParam>=VK_F1&&msg.wParam<=VK_F6){
            fixtureSnapshot.phase=static_cast<Phase>(msg.wParam-VK_F1);
            fixtureSnapshot.busy=msg.wParam>=VK_F2&&msg.wParam<=VK_F4;
            fixtureSnapshot.playable=msg.wParam==VK_F5;
            fixtureSnapshot.failure=msg.wParam==VK_F6?Failure::WriteFailed:Failure::None;
            UpdateRecordingIndicator(host,fixtureSnapshot);continue;
        }
        // Preview has no real shell/file actions.
        if(msg.message==WM_LBUTTONUP)continue;
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    return 0;
}
#else
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    DWORD warmed=0;
    for(int pass=0;pass<5;++pass)for(bool english:{false,true})for(bool light:{false,true}){
        HWND host=CreateWindowW(L"STATIC",L"Fixture",WS_OVERLAPPEDWINDOW,0,0,800,500,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        fixtureSnapshot={};AttachRecordingIndicator(host,host,english,light);
        HWND indicator=GetDlgItem(host,kIndicatorId);
        Check(indicator && GetParent(indicator)==host,"indicator is a child, not a standalone window");
        Check(!(GetWindowLongPtrW(indicator,GWL_STYLE)&WS_VISIBLE),"idle has no overlay");
        auto* state=reinterpret_cast<Indicator*>(GetWindowLongPtrW(indicator,GWLP_USERDATA));
        for(auto phase:{Phase::Starting,Phase::Recording,Phase::Finalizing,Phase::Saved,Phase::Failed}){
            fixtureSnapshot.phase=phase;fixtureSnapshot.busy=phase==Phase::Starting||phase==Phase::Recording||phase==Phase::Finalizing;
            fixtureSnapshot.elapsedMs=123000;fixtureSnapshot.playable=phase==Phase::Saved;
            fixtureSnapshot.failure=phase==Phase::Failed?Failure::WriteFailed:Failure::None;
            fixtureSnapshot.path=L"C:\\recordings\\fixture.mp4";
            UpdateRecordingIndicator(host,fixtureSnapshot);
            Check((GetWindowLongPtrW(indicator,GWL_STYLE)&WS_VISIBLE)!=0,"active and terminal states visible");
            if(phase==Phase::Recording)Check(state->text==L"● REC  00:02:03","compact stable recording label");
            if(phase==Phase::Recording){
                fixtureSnapshot.paused=true;UpdateRecordingIndicator(host,fixtureSnapshot);
                Check(state->text.find(english?L"PAUSED":L"일시정지")!=std::wstring::npos,"paused corner label");
                fixtureSnapshot.paused=false;UpdateRecordingIndicator(host,fixtureSnapshot);
            }
            if(phase==Phase::Finalizing)Check(!presentation::Build(fixtureSnapshot,english).canOpenVideo,"no open before finalize");
            if(phase==Phase::Failed)Check(state->text.find(presentation::FailureText(Failure::WriteFailed,english))!=std::wstring::npos,"failure reason retained");
            RECT client{},child{};GetClientRect(host,&client);GetWindowRect(indicator,&child);
            MapWindowPoints(nullptr,host,reinterpret_cast<POINT*>(&child),2);
            Check(child.left>=0&&child.top>=0&&child.right<=client.right&&child.bottom<=client.bottom,"corner fits host");
            HDC dc=GetDC(indicator);SendMessageW(indicator,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),0);ReleaseDC(indicator,dc);
            RECT bounds{};GetClientRect(indicator,&bounds);
            // The old Details region must not route to an empty settings page.
            SendMessageW(indicator,WM_LBUTTONUP,0,MAKELPARAM(bounds.right-1,bounds.bottom-1));
            MSG navigation{};
            const bool requested=PeekMessageW(&navigation,host,kOpenRecordingSettings,kOpenRecordingSettings,PM_REMOVE)!=FALSE;
            Check(requested==!Terminal(fixtureSnapshot),"only active status can open recording settings; removed details have no action");
            if(Terminal(fixtureSnapshot)&&!fixtureSnapshot.playable){
                SendMessageW(indicator,WM_LBUTTONUP,0,MAKELPARAM(Px(20,state->dpi),bounds.bottom-1));
                Check(!PeekMessageW(&navigation,host,kOpenRecordingSettings,kOpenRecordingSettings,PM_REMOVE),
                    "unverified failure has no obsolete details action");
            }
        }
        state->terminalUntil=0;UpdateRecordingIndicator(host,fixtureSnapshot);
        Check(!(GetWindowLongPtrW(indicator,GWL_STYLE)&WS_VISIBLE),"terminal notification expires");
        UpdateRecordingIndicator(host,fixtureSnapshot);
        Check(!(GetWindowLongPtrW(indicator,GWL_STYLE)&WS_VISIBLE),"unchanged result does not reopen");
        Check(stopCalls==0 && startCalls==0,"painting and closing indicator never control recording");
        DestroyWindow(host);
        Check(!IsWindow(indicator),"indicator lifetime belongs to playback surface");
        if(pass==0)warmed=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        else Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=warmed,"no GDI leak");
    }
    SetControlsClosing(true);Check(ToggleRecording()==ToggleResult::Stopping&&startCalls==0,"shutdown blocks restart");
    SetControlsClosing(false);Check(ToggleRecording()==ToggleResult::Starting&&startCalls==1,"normal toggle passes current options");
    puts("Recording child overlay: phases, expiry, ownership, no recorder side effects and resource lifetime passed.");
}
#endif
