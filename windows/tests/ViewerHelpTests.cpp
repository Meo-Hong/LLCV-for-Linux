#include "ui/ViewerHelpWindow.h"
#include "ui/AppPalette.h"
#include "ui/SettingsFonts.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <string>
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>
#include <vector>
#include <imm.h>
using namespace llcv::viewer_help;
namespace {
void Check(bool ok,const char* what) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
MSG Key(HWND target,UINT key,bool repeat=false) {
    MSG message{}; message.hwnd=target; message.message=WM_KEYDOWN;
    message.wParam=key; message.lParam=repeat ? (LPARAM{1}<<30) : 0; return message;
}
std::wstring Text(HWND window) {
    wchar_t text[4096]{}; GetWindowTextW(window,text,4096); return text;
}
void DrainNativeMessages() {
    // The real viewer continuously pumps native UI/IME cleanup messages.
    // Lifecycle tests must do likewise before sampling process-wide resources.
    MSG pending{};
    for (unsigned count=0;count<256 && PeekMessageW(&pending,nullptr,0,0,PM_REMOVE);++count) {
        Check(pending.message!=WM_QUIT,"help never posts quit while draining native cleanup");
        TranslateMessage(&pending);
        DispatchMessageW(&pending);
    }
}
BOOL CALLBACK FindRemainingHelpWindow(HWND hwnd,LPARAM found) {
    wchar_t name[64]{}; GetClassNameW(hwnd,name,64);
    if (!wcscmp(name,L"LLCV.ViewerHelp")) {
        *reinterpret_cast<bool*>(found)=true;
        return FALSE;
    }
    return TRUE;
}
BOOL CALLBACK TraceWindow(HWND hwnd,LPARAM) {
    wchar_t name[128]{}; GetClassNameW(hwnd,name,128);
    std::printf("NATIVE HWND=%p owner=%p class=",hwnd,GetWindow(hwnd,GW_OWNER));
    for (const wchar_t* p=name;*p;++p) std::putchar(*p<128 ? char(*p) : '?');
    std::puts("");
    return TRUE;
}
void CheckPalette(HWND body,bool light) {
    HIGHCONTRASTW hc{sizeof(hc)};
    const bool highContrast=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0) && (hc.dwFlags&HCF_HIGHCONTRASTON);
    struct Sample { int id; COLORREF surface,ink; };
    const auto p=llcv::ui::ResolvePalette(light,highContrast);
    const Sample samples[]{{201,p.kBackground,p.kText},{203,p.kBackground,p.kSecondary},
        {204,p.kCard,p.kText},{205,p.kControl,p.kAccent},{206,p.kCard,p.kText}};
    HDC dc=GetDC(body);
    for (const auto& sample:samples) {
        HBRUSH brush=reinterpret_cast<HBRUSH>(SendMessageW(body,WM_CTLCOLORSTATIC,
            reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(GetDlgItem(body,sample.id))));
        LOGBRUSH value{}; Check(GetObjectW(brush,sizeof(value),&value)!=0,"help surface brush");
        const COLORREF expectedBackground=highContrast ? GetSysColor(COLOR_WINDOW) : sample.surface;
        Check(value.lbColor==expectedBackground && GetBkColor(dc)==expectedBackground,"help uses shared app surfaces");
        Check(GetTextColor(dc)==sample.ink,"help uses shared app text/accent");
    }
    ReleaseDC(body,dc);
}
void CheckFields(HWND body) {
    RECT bounds{}; GetClientRect(body,&bounds);
    HDC dc=GetDC(body);
    for (HWND child=GetWindow(body,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
        RECT r{}; GetWindowRect(child,&r); MapWindowPoints(nullptr,body,reinterpret_cast<POINT*>(&r),2);
        Check(r.left>=0 && r.right<=bounds.right,"text stays within body width");
        const auto font=reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0));
        HGDIOBJ old=SelectObject(dc,font);
        RECT needed{0,0,r.right-r.left,0};
        const auto text=Text(child);
        DrawTextW(dc,text.c_str(),-1,&needed,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        Check(needed.bottom<=r.bottom-r.top,"all wrapped text fits control height");
        SelectObject(dc,old);
    }
    ReleaseDC(body,dc);
    // Each keyboard description is beside its key, not a prose paragraph.
    for (int i=0;i<8;++i) {
        RECT key{},action{};
        GetWindowRect(GetDlgItem(body,205+2*i),&key);
        GetWindowRect(GetDlgItem(body,206+2*i),&action);
        Check(key.right<action.left && abs(key.top-action.top)<8,"aligned key/action columns");
    }
}
void Snapshot(HWND window,const std::filesystem::path& path) {
    RECT r{}; GetWindowRect(window,&r); HDC dc=GetDC(window),memory=CreateCompatibleDC(dc);
    HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right-r.left,r.bottom-r.top);
    HGDIOBJ old=SelectObject(memory,bitmap);
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_NONCLIENT|PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    SelectObject(memory,old);
    UINT count=0,bytes=0; Gdiplus::GetImageEncodersSize(&count,&bytes);
    std::vector<BYTE> codecs(bytes); auto* list=reinterpret_cast<Gdiplus::ImageCodecInfo*>(codecs.data());
    Gdiplus::GetImageEncoders(count,bytes,list); bool saved=false;
    {
        Gdiplus::Bitmap image(bitmap,nullptr);
        for (UINT i=0;i<count;++i) if (!wcscmp(list[i].MimeType,L"image/png")) {
            saved=image.Save(path.c_str(),&list[i].Clsid)==Gdiplus::Ok; break;
        }
    }
    Check(saved,"rendered help preview saved");
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window,dc);
}
}
int main(int argc,char** argv) {
    // Diagnostic mode records a complete resource trajectory without changing
    // the normal regression assertion. It is not a passing leak certification.
    const bool resourceDiagnostics=argc>1 && std::strcmp(argv[1],"--resource-diagnostics")==0;
    const bool nativeIme=resourceDiagnostics || (argc>1 && std::strcmp(argv[1],"--with-ime")==0);
    const bool saveSnapshots=argc>1 && !nativeIme;
    // Isolate the strict process-wide resource assertion from Windows' IME
    // service, which lazily creates persistent UI after focus/key messages.
    // This affects only this test thread, never the app or user IME settings.
    // A separate --with-ime run keeps the real input path and verifies every
    // app-owned font/brush/window is released, without asserting on OS caches.
    if (!nativeIme) Check(ImmDisableIME(GetCurrentThreadId())!=FALSE,"isolate test-thread IME resources");
    const auto traceResources=[resourceDiagnostics](const char* stage,bool english,unsigned trial) {
        if (!resourceDiagnostics) return;
        std::printf("RESOURCE language=%s trial=%u stage=%s gdi=%lu user=%lu\n",
            english ? "en" : "ko",trial,stage,
            GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),
            GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS));
        std::fflush(stdout);
    };
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Gdiplus::GdiplusStartupInput startup; ULONG_PTR token=0;
    // The encoder owns background resources unrelated to help-window lifetime.
    // Do not start it in lifecycle tests that never save a preview image.
    if (saveSnapshots)
        Check(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok,"preview encoder");
    Window help;
    HWND owner=CreateWindowW(L"STATIC",L"Hidden help-test owner",WS_POPUP,
        0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(owner!=nullptr,"owner");
    Check(kHelpKey==VK_F1 && kScreenshotKey==VK_F12,"public shortcut bindings");
    DWORD stableGdi=0,stableUser=0;
    for (bool light:{false,true}) for (bool english:{false,true}) {
        Check(std::wcsstr(Shortcuts(english),L"F1 ") && std::wcsstr(Shortcuts(english),L"F12 ") &&
            !std::wcsstr(Shortcuts(english),L"F8 "),"one shared up-to-date shortcut list");
        for (unsigned trial=0;trial<20;++trial) {
            traceResources("before-open",english,trial);
            help.Toggle(owner,english,L"v-test",SW_HIDE,light);
            if (trial<3) traceResources("after-open",english,trial);
            HWND dialog=help.Handle(); Check(IsWindow(dialog)!=FALSE,"modeless creation");
            Check(GetWindow(dialog,GW_OWNER)==owner && IsWindowEnabled(owner),"owned, nonblocking, owner enabled");
            HWND edit=GetDlgItem(dialog,100);
            Check(IsWindow(edit) && !FindWindowExW(dialog,nullptr,L"EDIT",nullptr),"sectioned content replaces text box");
            std::wstring body;
            for (HWND field=GetWindow(edit,GW_CHILD);field;field=GetWindow(field,GW_HWNDNEXT)) body+=Text(field)+L"\n";
            Check(body.find(L"F12")!=std::wstring::npos && body.find(L"HDR")!=std::wstring::npos &&
                body.find(L"LowLatencyCaptureViewer")!=std::wstring::npos,"help includes screenshot details");
            Check(Text(GetDlgItem(edit,204))==(english ? L"Keyboard shortcuts" : L"키보드 단축키"),"keyboard section heading");
            Check(body.find(english ? L"Mouse controls" : L"마우스 조작")!=std::wstring::npos &&
                body.find(english ? L"Screenshots" : L"스크린샷")!=std::wstring::npos,"separate mouse and screenshot sections");
            Check(body.find(english ? L"Double-click" : L"두 번 클릭")==std::wstring::npos &&
                body.find(english ? L"Reset a volume control" : L"음량 영역을 100%로 복원")==std::wstring::npos,
                "common F1 guide omits audio-only double-click instructions");
            const HFONT font=reinterpret_cast<HFONT>(SendMessageW(edit,WM_GETFONT,0,0));
            LOGFONTW fontInfo{};
            Check(font && GetObjectW(font,sizeof(fontInfo),&fontInfo),"owned help font");
            Check(!wcscmp(fontInfo.lfFaceName,llcv::settings_ui::SettingsFontFamily(FW_MEDIUM,english)) &&
                fontInfo.lfWeight==FW_MEDIUM && fontInfo.lfQuality==CLEARTYPE_QUALITY,
                "help matches settings font family, weight and raster quality");
            Check(Text(GetDlgItem(edit,202))==L"v-test","runtime version badge");
            CheckFields(edit);
            CheckPalette(edit,light);
            help.SetLightTheme(!light); CheckPalette(edit,!light);
            help.SetLightTheme(light); CheckPalette(edit,light);
            std::vector<HFONT> ownedFonts;
            for (int id : {201,202,204,206}) {
                const auto handle=reinterpret_cast<HFONT>(SendMessageW(GetDlgItem(edit,id),WM_GETFONT,0,0));
                if (std::find(ownedFonts.begin(),ownedFonts.end(),handle)==ownedFonts.end())
                    ownedFonts.push_back(handle);
            }
            Check(ownedFonts.size()==4,"four distinct role fonts are owned");
            std::vector<HBRUSH> ownedBrushes;
            HDC colors=GetDC(edit);
            for (int id : {201,204,205}) ownedBrushes.push_back(reinterpret_cast<HBRUSH>(
                SendMessageW(edit,WM_CTLCOLORSTATIC,reinterpret_cast<WPARAM>(colors),
                    reinterpret_cast<LPARAM>(GetDlgItem(edit,id)))));
            ReleaseDC(edit,colors);
            const auto stem=std::string(light ? "help-light-" : "help-dark-")+(english ? "en" : "ko");
            if (saveSnapshots && !trial) Snapshot(dialog,std::filesystem::path(argv[1])/(stem+".png"));
            RECT initial{}; GetWindowRect(dialog,&initial);
            const UINT dpi=GetDpiForWindow(dialog);
            SetWindowPos(dialog,nullptr,0,0,MulDiv(400,dpi,96),MulDiv(400,dpi,96),SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
            CheckFields(edit);
            SendMessageW(edit,WM_VSCROLL,SB_BOTTOM,0);
            SCROLLINFO scroll{sizeof(scroll),SIF_ALL}; GetScrollInfo(edit,SB_VERT,&scroll);
            Check(scroll.nPos>0,"narrow layout scrolls to last section");
            CheckFields(edit);
            if (saveSnapshots && !trial) Snapshot(dialog,std::filesystem::path(argv[1])/(stem+"-narrow.png"));
            SetWindowPos(dialog,nullptr,0,0,initial.right-initial.left,initial.bottom-initial.top,SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
            CheckFields(edit);
            GetScrollInfo(edit,SB_VERT,&scroll);
            Check(scroll.nPos<=std::max(0,scroll.nMax-int(scroll.nPage)+1),"resize clamps scroll offset");
            auto shot=Key(edit,VK_F12);
            Check(!help.ProcessMessage(shot,owner,english,L"v-test",light) && IsScreenshotRequest(shot,false),"F12 reaches source screenshot handler");
            Check(!IsScreenshotRequest(shot,true),"audio-only screenshot ignored");
            shot=Key(edit,VK_F12,true); Check(!IsScreenshotRequest(shot,false),"held F12 ignored");
            shot=Key(edit,VK_F8); Check(!IsScreenshotRequest(shot,false),"old public F8 no longer captures");
            MSG wheel{}; wheel.hwnd=edit; wheel.message=WM_MOUSEWHEEL; wheel.wParam=MAKEWPARAM(0,WHEEL_DELTA);
            Check(help.ProcessMessage(wheel,owner,english,L"v-test",light),"help wheel never reaches volume fast path");
            auto tab=Key(edit,VK_TAB);
            Check(help.ProcessMessage(tab,owner,english,L"v-test",light),"help Tab never reaches diagnostic toggle");
            auto f1=Key(edit,VK_F1,true);
            Check(help.ProcessMessage(f1,owner,english,L"v-test",light) && help.Handle()==dialog,"F1 repeat does not toggle");
            auto escape=Key(edit,VK_ESCAPE);
            Check(help.ProcessMessage(escape,owner,english,L"v-test",light) && !help.Handle() && IsWindow(owner),"Esc closes only help");
            if (trial<3) traceResources("after-first-close",english,trial);
            Check(!GetObjectW(font,sizeof(fontInfo),&fontInfo),"help font released after child destruction");
            for (auto handle:ownedFonts)
                Check(!GetObjectW(handle,sizeof(fontInfo),&fontInfo),"every help role font released");
            LOGBRUSH brushInfo{};
            for (auto handle:ownedBrushes)
                Check(!GetObjectW(handle,sizeof(brushInfo),&brushInfo),"every help surface brush released");
            escape=Key(owner,VK_ESCAPE,true);
            Check(help.ProcessMessage(escape,owner,english,L"v-test"),"Esc repeat cannot exit viewer after dismiss");
            escape.message=WM_KEYUP;
            Check(help.ProcessMessage(escape,owner,english,L"v-test"),"dismiss key-up consumed");
            escape=Key(owner,VK_ESCAPE);
            Check(!help.ProcessMessage(escape,owner,english,L"v-test"),"next deliberate Esc retains viewer behavior");
            help.Toggle(owner,english,L"v-test",SW_HIDE,light);
            f1=Key(owner,VK_F1);
            Check(help.ProcessMessage(f1,owner,english,L"v-test") && !help.Handle(),"F1 closes existing help");
            help.Toggle(owner,english,L"v-test",SW_HIDE,light);
            SendMessageW(help.Handle(),WM_COMMAND,IDCANCEL,0);
            Check(!help.Handle() && IsWindow(owner),"close button preserves viewer");
            help.Toggle(owner,english,L"v-test",SW_HIDE,light);
            SendMessageW(help.Handle(),WM_CLOSE,0,0);
            Check(!help.Handle(),"title-bar close");
            DrainNativeMessages();
            bool remainingHelp=false;
            EnumThreadWindows(GetCurrentThreadId(),FindRemainingHelpWindow,
                reinterpret_cast<LPARAM>(&remainingHelp));
            Check(!remainingHelp,"all owned help windows were destroyed before resource sampling");
            const DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            const DWORD user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
            traceResources("after-four-lifecycles",english,trial);
            if (resourceDiagnostics && (trial==0 || trial==1 || trial==19))
                EnumThreadWindows(GetCurrentThreadId(),TraceWindow,0);
            // The isolated native renderer realizes one extra cached GDI
            // object during its first few focus/paint cycles. Warm 16 complete
            // lifecycles, then require a flat process-wide plateau; all owned
            // fonts, brushes and windows are checked even during warmup.
            if (!stableGdi || (!nativeIme && !light && !english && trial<4)) {
                stableGdi=gdi; stableUser=user;
            }
            else if ((gdi>stableGdi || user>stableUser) && (!nativeIme || resourceDiagnostics)) {
                std::fprintf(stderr,"RESOURCE above initial baseline: language=%s trial=%u gdi=%lu/%lu user=%lu/%lu\n",
                    english ? "en" : "ko",trial,gdi,stableGdi,user,stableUser);
                if (!nativeIme)
                    Check(false,"repeated help lifecycle does not leak GDI/USER objects");
            }
        }
    }
    help.Toggle(owner,false,L"v-test",SW_HIDE);
    HWND child=help.Handle(); DestroyWindow(owner);
    Check(!IsWindow(child) && !help.Handle(),"owner destruction clears help handle");
    help.Close();
    MSG quit{}; Check(!PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE),"help never posts quit");
    std::puts("F1 help lifecycle, Escape repeats, F12, focus routing and bilingual text passed.");
    if (token) Gdiplus::GdiplusShutdown(token);
    traceResources("after-shutdown",false,0);
}
