#include "ui/SettingsView.h"
#include "settings/SettingsStore.h"
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <set>
namespace keys=llcv::shortcuts;
using namespace llcv::settings_ui;
static void Check(bool ok,const char* message) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::abort(); }
}
static std::wstring Text(HWND h) {
    wchar_t text[1024]{};GetWindowTextW(h,text,1024);return text;
}
int main() {
    constexpr size_t recording=static_cast<size_t>(keys::Action::Recording);
    static_assert(keys::Count==10 && recording==9);
    Check(keys::Defaults[recording]==VK_F9 && keys::Valid(keys::Defaults),"F9 default");
    wchar_t temp[MAX_PATH]{},file[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH,temp)!=0 && GetTempFileNameW(temp,L"lks",0,file)!=0,"temporary profile");
    Check(WritePrivateProfileStringW(L"Shortcuts",L"Help",L"120",file)!=0,"legacy F9 help");
    auto settings=llcv::settings::LoadFromIni(file).settings;
    Check(settings.shortcuts[0]==VK_F9 && settings.shortcuts[recording]==VK_F10 &&
        keys::Valid(settings.shortcuts),"new binding preserves conflicting legacy customization");
    settings.shortcuts[recording]=keys::Ctrl|keys::Shift|'R';
    Check(llcv::settings::SaveToIni(file,settings),"save recording binding");
    Check(llcv::settings::LoadFromIni(file).settings.shortcuts==settings.shortcuts,"all bindings round-trip");
    Check(keys::Expand(L"F9 / F1",settings.shortcuts)==L"Ctrl+Shift+R / F9","help tokens do not cascade");
    settings.shortcuts=keys::Defaults;
    Check(llcv::settings::SaveToIni(file,settings) &&
        llcv::settings::LoadFromIni(file).settings.shortcuts==keys::Defaults,"reset persists");
    Check(WritePrivateProfileStringW(L"Shortcuts",L"Recording",L"0",file)!=0,"write invalid binding");
    Check(llcv::settings::LoadFromIni(file).settings.shortcuts==keys::Defaults,"recording cannot be empty");
    DeleteFileW(file);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    for (bool english:{false,true}) for (UINT dpi:{96u,120u,144u,192u}) {
        HWND owner=CreateWindowW(L"STATIC",L"Shortcut fixture",WS_POPUP,
            -10000,-10000,2400,1800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(owner!=nullptr,"UI fixture");
        SettingsControls s;s.english=english;s.activeTab=SettingsTab::GuideDiagnostics;
        s.tabControl=CreateWindowW(L"LISTBOX",L"",WS_CHILD|LBS_OWNERDRAWFIXED,
            0,0,100,100,owner,nullptr,nullptr,nullptr);
        auto child=[&](const wchar_t* kind,const wchar_t* text) {
            return CreateWindowW(kind,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,200,40,
                owner,nullptr,GetModuleHandleW(nullptr),nullptr);
        };
        s.guideShortcutsTitle=child(L"STATIC",L"Keyboard shortcuts");
        s.shortcutResetButton=child(L"BUTTON",L"Reset defaults");
        s.guideVideoHint=child(L"STATIC",L"");
        s.sectionNavigation=CreateWindowW(L"COMBOBOX",L"",kSettingsDropdownStyle,
            0,0,100,100,owner,nullptr,nullptr,nullptr);
        for (size_t i=0;i<keys::Count;++i) {
            s.guideKeys[i]=child(L"BUTTON",L"");
            const auto& description=llcv::viewer_help::kShortcuts[i];
            s.guideDescriptions[i]=child(L"STATIC",english?description.english:description.korean);
        }
        ApplySettingsFont(&s,owner,dpi);
        RefreshShortcutCaptions(&s);
        BeginShortcutEdit(&s,recording);
        auto press=[&](UINT key,unsigned modifiers=0,LPARAM flags=0) {
            MSG message{};message.hwnd=s.guideKeys[recording];message.message=WM_KEYDOWN;
            message.wParam=key;message.lParam=flags;
            return ProcessShortcutEdit(&s,message,modifiers);
        };
        Check(press(VK_F1) && s.editingShortcut==recording,"duplicate rejected");
        Check(press(0) && s.editingShortcut==recording,"blank rejected");
        Check(press('R',keys::Ctrl,LPARAM{1}<<30) && s.editingShortcut==recording,"repeat ignored");
        Check(press('R',keys::Ctrl) && s.editingShortcut==-1 &&
            s.shortcutBindings[recording]==(keys::Ctrl|'R'),"recording binding editable");
        Check(Text(s.guideKeys[recording])==L"Ctrl+R" &&
            Text(s.guideVideoHint).find(L"Ctrl+R")!=std::wstring::npos,"keycap and hint update");
        for (unsigned binding:{unsigned(VK_F9),keys::Ctrl|keys::Alt|keys::Shift|VK_SPACE}) {
            s.shortcutBindings[recording]=binding;RefreshShortcutCaptions(&s);
            for (SIZE size:{SIZE{1000,650},SIZE{740,540},SIZE{600,350}}) {
                FitSettingsClientArea(&s,{SettingsPixels(size.cx,dpi),SettingsPixels(size.cy,dpi)},dpi);
                LayoutSettingsControls(&s,dpi);
                UpdateAdvancedControlVisibility(&s,false,llcv::settings::VideoPixelFormat::Nv12);
                const int pages=SettingsCompactLayout(&s)?static_cast<int>(s.compactPageNames.size()):1;
                std::set<HWND> seen;
                for (int page=0;page<pages;++page) {
                    s.compactPages[SettingsNavigationIndex(s.activeTab)]=page;
                    UpdateAdvancedControlVisibility(&s,false,llcv::settings::VideoPixelFormat::Nv12);
                    std::vector<RECT> boxes;
                    std::vector<HWND> fields{s.guideVideoHint,s.shortcutResetButton};
                    fields.insert(fields.end(),s.guideKeys.begin(),s.guideKeys.end());
                    fields.insert(fields.end(),s.guideDescriptions.begin(),s.guideDescriptions.end());
                    for (HWND h:fields) {
                        if (!(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)) continue;
                        seen.insert(h);
                        RECT box{};GetWindowRect(h,&box);MapWindowPoints(nullptr,owner,reinterpret_cast<POINT*>(&box),2);
                        Check(box.bottom<=SettingsPixels(s.footerTopDip,dpi),"shortcut stays above footer");
                        for (RECT previous:boxes) {
                            RECT overlap{};
                            if (IntersectRect(&overlap,&box,&previous))
                                std::fprintf(stderr,"overlap language=%d dpi=%u size=%ldx%ld page=%d box=%ld,%ld,%ld,%ld other=%ld,%ld,%ld,%ld\n",
                                    english,dpi,size.cx,size.cy,page,box.left,box.top,box.right,box.bottom,
                                    previous.left,previous.top,previous.right,previous.bottom);
                            Check(!IntersectRect(&overlap,&box,&previous),"no shortcut overlap");
                        }
                        boxes.push_back(box);
                        if (h==s.guideVideoHint || h==s.guideKeys[recording]) {
                            HDC dc=GetDC(h);auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)));
                            RECT needed{0,0,box.right-box.left,0};auto text=Text(h);
                            DrawTextW(dc,text.c_str(),-1,&needed,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                            Check(needed.bottom<=box.bottom-box.top,"full hint/key text fits");
                            SelectObject(dc,old);ReleaseDC(h,dc);
                        }
                    }
                }
                Check(seen.contains(s.guideKeys[recording]) && seen.contains(s.guideVideoHint),"recording and hint reachable");
                Check(!(GetWindowLongPtrW(owner,GWL_STYLE)&(WS_VSCROLL|WS_HSCROLL)),"no scrollbars");
            }
        }
        DestroyWindow(owner);for (auto font:s.uiFonts)DeleteObject(font);
    }
    puts("Recording shortcut persistence, migration, editing and KO/EN/DPI/no-scroll layout passed.");
}
