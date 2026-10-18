#include "recording/Recorder.h"
#include "recording/RecordingStatusPresentation.h"
#include "ui/AppPalette.h"
#include "ui/SettingsFonts.h"
#include <commctrl.h>
#include <shellapi.h>
#include <filesystem>
#include <new>

namespace llcv::recording {
namespace {
bool controlsClosing=false;
constexpr int kIndicatorId=2090;
constexpr wchar_t kIndicatorClass[]=L"LLCVRecordingIndicator";
struct Indicator {
    HWND viewer{};
    bool english=false,light=false;
    UINT dpi=96;
    HFONT font{};
    Snapshot snapshot{};
    uint64_t terminalUntil=0;
    std::wstring text;
};
int Px(int value,UINT dpi){return MulDiv(value,static_cast<int>(dpi),96);}
bool Terminal(const Snapshot& s){return !s.busy && (s.phase==Phase::Saved || s.phase==Phase::Failed);}
void Position(HWND hwnd,Indicator& s) {
    RECT area{};GetClientRect(GetParent(hwnd),&area);
    const UINT dpi=GetDpiForWindow(hwnd);
    if(!s.font || s.dpi!=dpi){
        auto font=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,
            settings_ui::SettingsFontFamily(FW_MEDIUM,s.english));
        if(font){if(s.font)DeleteObject(s.font);s.font=font;s.dpi=dpi;}
    }
    const bool terminal=Terminal(s.snapshot);
    const int width=(std::min)(Px(terminal?380:200,s.dpi),static_cast<int>((std::max)(1L,area.right-Px(24,s.dpi))));
    const bool problem=s.snapshot.failure!=Failure::None || s.snapshot.phase==Phase::Failed;
    const int height=(std::min)(Px(terminal?(problem?128:68):36,s.dpi),static_cast<int>((std::max)(1L,area.bottom)));
    SetWindowPos(hwnd,HWND_TOP,(std::max)(0L,area.right-width-Px(12,s.dpi)),
        (std::max)(0L,area.bottom-height-Px(12,s.dpi)),width,height,SWP_NOACTIVATE);
    auto region=CreateRoundRectRgn(0,0,width+1,height+1,Px(14,s.dpi),Px(14,s.dpi));
    if(region && !SetWindowRgn(hwnd,region,TRUE))DeleteObject(region);
}
void Paint(HWND hwnd,HDC dc,Indicator& s){
    RECT area{};GetClientRect(hwnd,&area);
    const auto p=ui::ResolvePalette(s.light,ui::HighContrastEnabled());
    const auto oldFont=SelectObject(dc,s.font);
    auto brush=CreateSolidBrush(p.kCard);FillRect(dc,&area,brush);DeleteObject(brush);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,s.snapshot.phase==Phase::Recording ||
        s.snapshot.failure!=Failure::None || s.snapshot.phase==Phase::Failed?p.kDanger:p.kText);
    RECT text=area;InflateRect(&text,-Px(12,s.dpi),-Px(8,s.dpi));
    if(Terminal(s.snapshot))text.bottom=area.bottom-Px(36,s.dpi);
    DrawTextW(dc,s.text.c_str(),-1,&text,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
    if(Terminal(s.snapshot) && presentation::Build(s.snapshot,s.english).canOpenVideo){
        RECT actions{Px(12,s.dpi),area.bottom-Px(28,s.dpi),area.right-Px(12,s.dpi),area.bottom};
        SetTextColor(dc,p.kText);
        const wchar_t* label=s.english?L"Open video    ·    Open folder":L"영상 열기    ·    폴더 열기";
        DrawTextW(dc,label,-1,&actions,DT_SINGLELINE|DT_NOPREFIX);
    }
    SelectObject(dc,oldFont);
}
LRESULT CALLBACK IndicatorProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    auto* s=reinterpret_cast<Indicator*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    switch(msg){
    case WM_NCCREATE:{
        const auto* source=static_cast<Indicator*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        s=new(std::nothrow) Indicator(*source);if(!s)return FALSE;
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));return TRUE;
    }
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);if(s)Paint(hwnd,dc,*s);EndPaint(hwnd,&ps);return 0;}
    case WM_PRINTCLIENT:if(s)Paint(hwnd,reinterpret_cast<HDC>(w),*s);return 0;
    case WM_LBUTTONUP:
        if(s){
            RECT area{};GetClientRect(hwnd,&area);
            const int x=static_cast<short>(LOWORD(l)),y=static_cast<short>(HIWORD(l));
            const auto view=presentation::Build(recorder.GetSnapshot(),s->english);
            if(Terminal(s->snapshot) && y>=area.bottom-Px(32,s->dpi) && view.canOpenVideo && x<Px(220,s->dpi))
                OpenRecording(s->viewer,x<Px(105,s->dpi),s->english);
            else if(!Terminal(s->snapshot))PostMessageW(s->viewer,kOpenRecordingSettings,0,0);
        }return 0;
    case WM_NCDESTROY:
        if(s){if(s->font)DeleteObject(s->font);delete s;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);}break;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
LRESULT CALLBACK HostProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
    const auto result=DefSubclassProc(hwnd,msg,w,l);
    if(msg==WM_SIZE || msg==WM_DPICHANGED_AFTERPARENT){
        auto child=GetDlgItem(hwnd,kIndicatorId);
        auto* s=reinterpret_cast<Indicator*>(GetWindowLongPtrW(child,GWLP_USERDATA));
        if(s)Position(child,*s);
    }
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(hwnd,HostProc,id);
    return result;
}
}
ToggleResult ToggleRecording() {
    if(controlsClosing){if(recorder.Busy())recorder.Stop();return ToggleResult::Stopping;}
    if(recorder.Busy()){recorder.Stop();return ToggleResult::Stopping;}
    const auto folder=sessionOptions.folder.empty()?DefaultFolder():sessionOptions.folder;
    if(folder.empty())return ToggleResult::Failed;
    SYSTEMTIME t{};GetLocalTime(&t);
    wchar_t name[100]{};
    swprintf_s(name,L"\\LLCV-%04u%02u%02u-%02u%02u%02u-%03u.mp4",
        t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    return recorder.Start(folder+name,&sessionOptions)?ToggleResult::Starting:ToggleResult::Failed;
}
void SetControlsClosing(bool closing){controlsClosing=closing;}
bool ControlsClosing(){return controlsClosing;}
void OpenRecording(HWND owner,bool video,bool english){
    // Revalidate on click: the previous session may have been replaced.
    const auto snapshot=recorder.GetSnapshot();
    const auto view=presentation::Build(snapshot,english);
    if(video?!view.canOpenVideo:!view.canOpenFolder)return;
    const auto path=video?snapshot.path:std::filesystem::path(snapshot.path).parent_path().wstring();
    if(path.empty())return;
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(owner,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
        MessageBoxW(owner,english?L"The file or folder could not be opened.":L"파일 또는 폴더를 열지 못했습니다.",L"LLCV",MB_OK|MB_ICONWARNING);
}
void AttachRecordingIndicator(HWND host,HWND viewer,bool english,bool light){
    if(!host || GetDlgItem(host,kIndicatorId))return;
    WNDCLASSW wc{};wc.lpfnWndProc=IndicatorProc;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=kIndicatorClass;wc.hCursor=LoadCursorW(nullptr,IDC_HAND);
    RegisterClassW(&wc);
    Indicator initial;initial.viewer=viewer;initial.english=english;initial.light=light;
    // A child of the playback surface, never a separate status/taskbar window.
    // Recorder::Push consumes raw DirectShow samples, not window/screen pixels.
    const auto child=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_CHILD,
        0,0,1,1,host,reinterpret_cast<HMENU>(INT_PTR(kIndicatorId)),wc.hInstance,&initial);
    if(child){SetWindowSubclass(host,HostProc,kIndicatorId,0);UpdateRecordingIndicator(host,recorder.GetSnapshot());}
}
void UpdateRecordingIndicator(HWND host,const Snapshot& snapshot){
    const auto child=GetDlgItem(host,kIndicatorId);
    auto* s=reinterpret_cast<Indicator*>(GetWindowLongPtrW(child,GWLP_USERDATA));
    if(!s)return;
    const bool transition=s->snapshot.phase!=snapshot.phase || s->snapshot.path!=snapshot.path ||
        s->snapshot.failure!=snapshot.failure || s->snapshot.busy!=snapshot.busy;
    if(transition && Terminal(snapshot))s->terminalUntil=GetTickCount64()+10000;
    s->snapshot=snapshot;
    const bool visible=snapshot.busy || (Terminal(snapshot)&&GetTickCount64()<s->terminalUntil);
    const bool shown=(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)!=0;
    if(!visible){if(shown)ShowWindow(child,SW_HIDE);return;}
    auto view=presentation::Build(snapshot,s->english);
    std::wstring text;
    if(snapshot.phase==Phase::Recording)text=(snapshot.paused?(s->english?L"Ⅱ PAUSED  ":L"Ⅱ 일시정지  "):L"● REC  ")+presentation::Elapsed(snapshot.elapsedMs);
    else if(Terminal(snapshot)){
        text=view.title;
        if(snapshot.failure!=Failure::None || snapshot.phase==Phase::Failed)text+=L"\n"+view.description;
    }else text=view.title;
    if(s->text!=text || transition){s->text=std::move(text);SetWindowTextW(child,s->text.c_str());InvalidateRect(child,nullptr,FALSE);}
    if(transition || !shown)Position(child,*s);
    if(!shown)ShowWindow(child,SW_SHOWNOACTIVATE);
}
}
