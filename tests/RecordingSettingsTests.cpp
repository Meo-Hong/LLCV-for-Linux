#include "recording/RecordingSettingsView.h"
#include "ui/SettingsTheme.h"
#include "settings/AppSettings.h"
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <set>
using namespace llcv::settings_ui;
static void Check(bool value,const char* what){if(!value){printf("FAIL %s\n",what);std::abort();}}
static std::wstring Text(HWND control){wchar_t text[512]{};GetWindowTextW(control,text,512);return text;}
static LRESULT CALLBACK Changes(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    if(m==WM_ENABLE)++*reinterpret_cast<int*>(data);return DefSubclassProc(h,m,w,l);
}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    for(bool english:{false,true})for(UINT dpi:{96u,120u,144u,192u}){
        auto owner=CreateWindowW(L"STATIC",L"Recording UI test",WS_OVERLAPPEDWINDOW,0,0,2000,1400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        SettingsControls s;s.english=english;s.activeTab=SettingsTab::VideoWindow;
        s.tabControl=CreateWindowW(L"LISTBOX",L"",WS_CHILD|LBS_OWNERDRAWFIXED,0,0,100,100,owner,nullptr,nullptr,nullptr);
        s.sectionNavigation=CreateWindowW(L"COMBOBOX",L"",kSettingsDropdownStyle,0,0,100,100,owner,nullptr,nullptr,nullptr);
        CreateRecordingSettings(&s,owner);ApplySettingsFont(&s,owner,dpi);
        Check(Text(s.recSpace).empty(),"opening another page does not query recording storage");
        s.activeTab=SettingsTab::Recording;ShowRecordingSettings(&s);
        Check(!Text(s.recSpace).empty(),"opening recording page populates storage information");
        SetWindowTextW(s.recSpace,L"Storage query sentinel");
        RefreshRecordingSettings(&s);ShowRecordingSettings(&s);
        Check(Text(s.recSpace)==L"Storage query sentinel","unrelated option refresh does not repeat disk query");
        s.activeTab=SettingsTab::VideoWindow;ShowRecordingSettings(&s);
        s.activeTab=SettingsTab::Recording;ShowRecordingSettings(&s);
        Check(Text(s.recSpace)!=L"Storage query sentinel","returning to recording refreshes storage information");
        const auto recordingNote=Text(s.recNote);
        Check(recordingNote.find(L"CQP")!=std::wstring::npos&&recordingNote.find(L"MP4")!=std::wstring::npos,"essential recording guidance retained");
        Check(recordingNote.find(L"NV12")!=std::wstring::npos && recordingNote.find(L"YUY2")!=std::wstring::npos &&
            recordingNote.find(L"PCM")!=std::wstring::npos,"supported recording input formats are explicit");
        Check(!IsWindowEnabled(s.recordingCodecs[0]),"pending probe disables codecs");
        s.recordingProbePending=false;
        s.recordingCapabilities[0]={true,true,{{L"Test hardware",L"test"}},{true,true},{true,true}};
        s.recordingCapabilities[1]={true,false,{{L"Test HEVC",L"test"}},{true,true},{}};
        // An installed encoder without MP4 mux support must stay disabled.
        s.recordingCapabilities[2]={false,true,{}};RefreshRecordingSettings(&s);
        Check(IsWindowEnabled(s.recordingCodecs[0])&&IsWindowEnabled(s.recordingCodecs[1])&&!IsWindowEnabled(s.recordingCodecs[2]),"support gate");
        SendMessageW(s.recMode,CB_SHOWDROPDOWN,TRUE,0);
        Check(SendMessageW(s.recMode,CB_GETDROPPEDSTATE,0,0)!=0,"rate control dropdown open");
        SendMessageW(s.recMode,CB_SETCURSEL,1,0);
        RecordingSettingsCommand(&s,owner,2069,CBN_SELCHANGE);
        Check(s.recordingOptions.rateControl==llcv::recording::RateControl::Quality,"dropdown preview does not change accepted options");
        RecordingSettingsCommand(&s,owner,2069,CBN_SELENDCANCEL);
        SendMessageW(s.recMode,CB_SHOWDROPDOWN,FALSE,0);
        RefreshRecordingSettings(&s);
        Check(SendMessageW(s.recMode,CB_GETCURSEL,0,0)==0,"cancel restores accepted mode");
        SendMessageW(s.recMode,CB_SETCURSEL,1,0);
        RecordingSettingsCommand(&s,owner,2069,CBN_SELENDOK);
        Check(s.recordingOptions.rateControl==llcv::recording::RateControl::Bitrate,"dropdown acceptance commits selection");
        SendMessageW(s.recMode,CB_SETCURSEL,0,0);RecordingSettingsCommand(&s,owner,2069,CBN_SELENDOK);
        RecordingSettingsCommand(&s,owner,2063,BN_CLICKED);
        Check(s.recordingOptions.codec==llcv::recording::Codec::H264,"disabled codec cannot be selected");
        RecordingSettingsCommand(&s,owner,2062,BN_CLICKED);
        Check(s.recordingOptions.codec==llcv::recording::Codec::Hevc,"codec selection");
        Check(SendMessageW(s.recEncoder,CB_GETCOUNT,0,0)==1,"unsupported CPU encoder is not selectable");
        SendMessageW(s.recMode,CB_SETCURSEL,1,0);RecordingSettingsCommand(&s,owner,2069,CBN_SELCHANGE);
        Check(s.recordingOptions.rateControl==llcv::recording::RateControl::Bitrate,"manual bitrate mode selected");
        Check(SendMessageW(s.recordingCodecs[0],BM_GETCHECK,0,0)==BST_UNCHECKED&&SendMessageW(s.recordingCodecs[1],BM_GETCHECK,0,0)==BST_CHECKED,"exactly one codec checked");
        SendMessageW(s.recFps,CB_SETCURSEL,2,0);RecordingSettingsCommand(&s,owner,2065,CBN_SELCHANGE);
        Check(s.recordingOptions.maxFps==30,"recording fps selection");
        SendMessageW(s.recBitrate,CB_SETCURSEL,8,0);RecordingSettingsCommand(&s,owner,2066,CBN_SELCHANGE);
        Check(s.recordingOptions.videoMbps==10,"bitrate selection");
        SendMessageW(s.recAudio,CB_SETCURSEL,0,0);RecordingSettingsCommand(&s,owner,2067,CBN_SELCHANGE);
        Check(s.recordingOptions.audioKbps==128,"audio bitrate selection retained");
        SendMessageW(s.recFps,CB_SETCURSEL,0,0);RecordingSettingsCommand(&s,owner,2065,CBN_SELCHANGE);
        Check(s.recordingOptions.maxFps==0,"match-capture selection restored");
        Check(Text(s.recNote)==recordingNote,"guidance remains stable when recording settings change");
        Check(!IsWindowEnabled(s.recSplitValue)&&!IsWindowEnabled(s.recPause),"default splitting off and pause unavailable before capture");
        Check(!(GetWindowLongPtrW(s.recPause,GWL_STYLE)&WS_VISIBLE),"idle settings hide recording pause");
        SendMessageW(s.recResolution,CB_SETCURSEL,3,0);RecordingSettingsCommand(&s,owner,kRecordingResolution,CBN_SELCHANGE);
        Check(s.recordingOptions.maxHeight==720,"recording resolution selection");
        SendMessageW(s.recSplit,CB_SETCURSEL,1,0);RecordingSettingsCommand(&s,owner,kRecordingSplit,CBN_SELCHANGE);
        Check(IsWindowEnabled(s.recSplitValue),"split threshold enabled");
        SendMessageW(s.recSplitValue,CB_SETCURSEL,0,0);RecordingSettingsCommand(&s,owner,kRecordingSplitValue,CBN_SELCHANGE);
        Check(s.recordingOptions.splitSeconds==900,"time split selection");
        SendMessageW(s.recSplit,CB_SETCURSEL,2,0);RecordingSettingsCommand(&s,owner,kRecordingSplit,CBN_SELCHANGE);
        SendMessageW(s.recSplitValue,CB_SETCURSEL,3,0);RecordingSettingsCommand(&s,owner,kRecordingSplitValue,CBN_SELCHANGE);
        Check(s.recordingOptions.splitMiB==8192&&s.recordingOptions.splitSeconds==900,"independent size and time thresholds");
        for(bool size:{false,true}){
            s.recordingOptions.splitMode=size?llcv::recording::SplitMode::Size:llcv::recording::SplitMode::Time;
            s.recordingOptions.splitMiB=1536;s.recordingOptions.splitSeconds=75;
            const auto options=s.recordingOptions;
            RefreshRecordingSettings(&s);
            const auto selected=SendMessageW(s.recSplitValue,CB_GETCURSEL,0,0);
            Check(selected!=CB_ERR && SendMessageW(s.recSplitValue,CB_GETITEMDATA,selected,0)==(size?1536:75),
                "custom saved split threshold is visible and exact");
            Check(Text(s.recSplitValue).find(size?L"1536":L"75")!=std::wstring::npos,
                "custom split caption preserves units without rounding");
            RecordingSettingsCommand(&s,owner,kRecordingSplitValue,CBN_SELENDOK);
            Check(s.recordingOptions==options,"accepting custom threshold preserves stored settings");
            const auto count=SendMessageW(s.recSplitValue,CB_GETCOUNT,0,0);
            RefreshRecordingSettings(&s);
            Check(SendMessageW(s.recSplitValue,CB_GETCOUNT,0,0)==count,"custom threshold is not duplicated on refresh");
        }
        for(auto h:s.recordingControls)
            Check(Text(h).find(english?L"Estimated size":L"예상 용량")==std::wstring::npos,"no size estimate UI remains");
        Check(llcv::recording::sessionOptions==llcv::recording::Options{},"editing does not mutate accepted session");
        int transitions=0;
        for(auto h:s.recordingControls)SetWindowSubclass(h,Changes,1,reinterpret_cast<DWORD_PTR>(&transitions));
        for(int i=0;i<50;++i)ShowRecordingSettings(&s);
        Check(transitions==0,"unchanged capability causes no enable churn");
        s.recordingLive=true;s.recordingSnapshot.phase=llcv::recording::Phase::Recording;s.recordingSnapshot.busy=true;
        s.recordingSnapshot.elapsedMs=123000;
        ShowRecordingSettings(&s);
        Check(CanToggleRecordingSettings(&s)&&IsWindowEnabled(s.recOpenFolder),"recording shortcut can stop and save folder stays available");
        const auto busyOptions=s.recordingOptions;
        Check(IsWindowEnabled(s.recPause)&&!IsWindowEnabled(s.recSplit)&&!IsWindowEnabled(s.recResolution),"only pause available while busy");
        Check((GetWindowLongPtrW(s.recPause,GWL_STYLE)&WS_VISIBLE)!=0,"pause appears during recording");
        s.recordingSnapshot.paused=true;ShowRecordingSettings(&s);
        Check(Text(s.recPause)==(english?L"Resume recording":L"녹화 재개"),"resume caption");
        RecordingSettingsCommand(&s,owner,2061,BN_CLICKED);
        Check(s.recordingOptions==busyOptions&&!IsWindowEnabled(s.recEncoder),"busy options locked");
        transitions=0;for(int i=0;i<50;++i)ShowRecordingSettings(&s);
        Check(transitions==0,"busy polling never toggles control enabled state");
        s.recordingSnapshot.phase=llcv::recording::Phase::Finalizing;ShowRecordingSettings(&s);
        Check(!IsWindowEnabled(s.recPause),"pause blocked during finalization");
        Check(!(GetWindowLongPtrW(s.recPause,GWL_STYLE)&WS_VISIBLE),"pause hidden during finalization");
        Check(!CanToggleRecordingSettings(&s)&&IsWindowEnabled(s.recOpenFolder),"finalization locks recording shortcut but not folder access");
        s.recordingSnapshot.phase=llcv::recording::Phase::Saved;s.recordingSnapshot.busy=false;
        s.recordingSnapshot.path=L"C:\\recordings\\test.mp4";s.recordingSnapshot.playable=true;
        ShowRecordingSettings(&s);Check(CanToggleRecordingSettings(&s)&&IsWindowEnabled(s.recOpenFolder),"saved state allows restart and folder access");
        s.recordingSnapshot={};s.recordingLive=false;ShowRecordingSettings(&s);
        for(bool live:{false,true})for(auto mode:{llcv::recording::RateControl::Quality,llcv::recording::RateControl::Bitrate}){
        s.recordingLive=live;s.recordingSnapshot.busy=live;
        s.recordingSnapshot.phase=live?llcv::recording::Phase::Recording:llcv::recording::Phase::Idle;
        s.recordingOptions.rateControl=mode;RefreshRecordingSettings(&s);
        for(SIZE dips:{SIZE{1000,650},SIZE{740,540},SIZE{640,430},SIZE{600,350}}){
            FitSettingsClientArea(&s,{SettingsPixels(dips.cx,dpi),SettingsPixels(dips.cy,dpi)},dpi);
            LayoutSettingsControls(&s,dpi);UpdateAdvancedControlVisibility(&s,false,llcv::settings::VideoPixelFormat::Nv12);
            const int pages=SettingsCompactLayout(&s)?static_cast<int>(s.compactPageNames.size()):1;
            std::set<HWND> seen;
            for(int page=0;page<pages;++page){
                s.compactPages[SettingsNavigationIndex(SettingsTab::Recording)]=page;
                UpdateAdvancedControlVisibility(&s,false,llcv::settings::VideoPixelFormat::Nv12);
                std::vector<RECT> rectangles;
                for(auto h:s.recordingControls){
                    if(!(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE))continue;
                    seen.insert(h);RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,owner,reinterpret_cast<POINT*>(&r),2);
                    Check(r.left>=SettingsPixels(s.sidebarWidthDip,dpi)&&r.right<=SettingsPixels(s.clientWidthDip,dpi),"horizontal bounds");
                    Check(r.top>=0&&r.bottom<=SettingsPixels(s.footerTopDip,dpi),"vertical bounds");
                    wchar_t className[32]{};GetClassNameW(h,className,32);
                    // Check all explanatory copy, including the bottom-right guidance.
                    // The folder intentionally uses native path ellipsis.
                    if(wcscmp(className,L"Static")==0 && h!=s.recFolder){
                        HDC dc=GetDC(h);auto previous=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)));
                        RECT needed{0,0,r.right-r.left,0};const auto text=Text(h);
                        DrawTextW(dc,text.c_str(),-1,&needed,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                        SelectObject(dc,previous);ReleaseDC(h,dc);
                        Check(needed.bottom<=r.bottom-r.top,"recording text fits without clipping");
                    }
                    for(auto previous:rectangles){RECT overlap{};Check(!IntersectRect(&overlap,&r,&previous),"recording controls do not overlap");}rectangles.push_back(r);
                }
            }
            for(auto h:s.recordingControls)if(h!=s.recVideoTitle&&h!=s.recSaveTitle&&(h!=s.recPause||live))Check(seen.contains(h),"every option reachable without scrolling");
            RECT resolution{},fps{};GetWindowRect(s.recResolution,&resolution);GetWindowRect(s.recFps,&fps);
            Check(resolution.top==fps.top&&resolution.bottom==fps.bottom&&resolution.right<fps.left,
                "resolution and frame rate remain together");
            Check(!(GetWindowLongPtrW(owner,GWL_STYLE)&(WS_VSCROLL|WS_HSCROLL)),"no dialog scrollbars");
        }
        s.recordingLive=false;s.recordingSnapshot={};
        }
        FitSettingsClientArea(&s,{SettingsPixels(1000,dpi),SettingsPixels(650,dpi)},dpi);
        LayoutSettingsControls(&s,dpi);UpdateAdvancedControlVisibility(&s,false,llcv::settings::VideoPixelFormat::Nv12);
        RECT browse{},open{};
        GetWindowRect(s.recBrowse,&browse);GetWindowRect(s.recOpenFolder,&open);
        Check(open.left>browse.right&&open.top==browse.top&&open.bottom==browse.bottom&&
            open.right-open.left==browse.right-browse.left,"folder actions aligned side by side at every DPI");
        Check(GetDlgItem(owner,kRecordingToggleCommand)==nullptr&&GetDlgItem(owner,2072)==nullptr,
            "removed status actions are not created");
        Check(IsWindowEnabled(s.recOpenFolder),"save folder is available before any recording");
        Check(!CanToggleRecordingSettings(&s),"startup settings cannot start capture");
        s.recordingLive=true;s.recordingClosing=true;
        Check(!CanToggleRecordingSettings(&s),"closing blocks recording shortcut");
        s.recordingClosing=false;s.recordingLive=false;
        const HWND tabOrder[]={s.recEncoder,s.recMode,s.recBitrate,s.recResolution,s.recFps,s.recAudio,s.recBrowse,s.recOpenFolder,s.recSplit,s.recSplitValue};
        for(size_t i=1;i<std::size(tabOrder);++i)
            Check(GetNextDlgTabItem(owner,tabOrder[i-1],FALSE)==tabOrder[i],"keyboard traversal follows recording layout");
        s.recordingCapabilities[1].hardwareRates.quality=false;
        s.recordingOptions.rateControl=llcv::recording::RateControl::Quality;RefreshRecordingSettings(&s);
        Check(SendMessageW(s.recMode,CB_GETCOUNT,0,0)==1&&SendMessageW(s.recMode,CB_GETCURSEL,0,0)==CB_ERR,"unsupported saved mode is not silently replaced");
        Check(!IsWindowEnabled(s.recBitrate),"unsupported mode disables value selection");
        s.recordingLive=true;Check(!CanToggleRecordingSettings(&s),"unsupported mode blocks recording shortcut");s.recordingLive=false;
        SendMessageW(s.recMode,CB_SETCURSEL,0,0);RecordingSettingsCommand(&s,owner,2069,CBN_SELCHANGE);
        Check(s.recordingOptions.rateControl==llcv::recording::RateControl::Bitrate,"only supported mode can be selected");
        s.recordingCapabilities[1].hardwareRates.quality=true;RefreshRecordingSettings(&s);
        SendMessageW(s.recMode,CB_SETCURSEL,0,0);RecordingSettingsCommand(&s,owner,2069,CBN_SELCHANGE);
        SendMessageW(s.recBitrate,CB_SETCURSEL,2,0);RecordingSettingsCommand(&s,owner,2066,CBN_SELCHANGE);
        Check(s.recordingOptions.quality==llcv::recording::Quality::High&&s.recordingOptions.videoMbps==10,"mode switch preserves independent bitrate and quality values");
        s.activeTab=SettingsTab::Audio;UpdateAdvancedControlVisibility(&s,false,llcv::settings::VideoPixelFormat::Nv12);
        for(auto h:s.recordingControls)Check(!(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)&&!IsWindowEnabled(h),"hidden recording controls are disabled");
        DestroyWindow(owner);for(auto font:s.uiFonts)DeleteObject(font);
    }
    puts("Recording settings: KO/EN, 4 DPI scales, 4 work areas, support gates, selection and no-scroll layout passed.");
}
