#include "ViewerHelpWindow.h"
#include "SettingsFonts.h"
#include <algorithm>
#include <dwmapi.h>

namespace llcv::viewer_help {
namespace {
constexpr wchar_t kClass[]=L"LLCV.ViewerHelp", kBodyClass[]=L"LLCV.ViewerHelp.Content";
enum Index : unsigned {
    Tag,Name,Version,Subtitle,KeyboardTitle,FirstKey,VideoHint=FirstKey+2*kShortcuts.size(),
    MouseTitle,WheelLabel,WheelText,DragLabel,DragText,
    ShotTitle,ShotSummary,FolderLabel,FolderText,ShotOption,FieldCount
};
int Pixels(int dip,UINT dpi) { return MulDiv(dip,dpi ? dpi : 96,96); }
}
Window::~Window() { Close(); }
void Window::Close() {
    if (!window_) return;
    const HWND owner=owner_;
    const bool active=GetActiveWindow()==window_;
    DestroyWindow(window_);
    if (active && IsWindow(owner) && IsWindowEnabled(owner)) SetFocus(owner);
}
void Window::ApplyFont() {
    const UINT dpi=GetDpiForWindow(window_);
    std::array<HFONT,FontCount> next{};
    const int points[]{10,10,20,9};
    for (int i=0;i<FontCount;++i) {
        const int weight=i==Strong || i==Title ? FW_SEMIBOLD : i==Normal ? FW_MEDIUM : FW_NORMAL;
        next[i]=CreateFontW(-MulDiv(points[i],dpi,72),0,0,0,weight,
            FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,DEFAULT_PITCH,settings_ui::SettingsFontFamily(weight,english_));
        if (!next[i]) { for (auto font:next) if (font) DeleteObject(font); return; }
    }
    for (const auto& field:fields_) SendMessageW(field.hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(next[field.font]),FALSE);
    for (HWND child:{footer_,close_}) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(next[Small]),FALSE);
    for (auto font:fonts_) if (font) DeleteObject(font);
    fonts_=next;
}
bool Window::UpdateTheme() {
    const bool highContrast=ui::HighContrastEnabled();
    const auto palette=ui::ResolvePalette(lightTheme_,highContrast);
    const COLORREF colors[]{palette.kBackground,palette.kCard,palette.kControl};
    std::array<HBRUSH,SurfaceCount> next{};
    for (int i=0;i<SurfaceCount;++i) {
        next[i]=CreateSolidBrush(colors[i]);
        if (!next[i]) { for (auto brush:next) if (brush) DeleteObject(brush); return false; }
    }
    for (auto brush:brushes_) if (brush) DeleteObject(brush);
    brushes_=next; palette_=palette; highContrast_=highContrast;
    const BOOL dark=!highContrast_ && !lightTheme_;
    DwmSetWindowAttribute(window_,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    const COLORREF caption=highContrast_ ? DWMWA_COLOR_DEFAULT : palette_.kBackground;
    const COLORREF text=highContrast_ ? DWMWA_COLOR_DEFAULT : palette_.kText;
    DwmSetWindowAttribute(window_,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
    DwmSetWindowAttribute(window_,DWMWA_TEXT_COLOR,&text,sizeof(text));
    return true;
}
void Window::SetLightTheme(bool lightTheme) {
    if (lightTheme_==lightTheme) return;
    const bool previous=lightTheme_;
    lightTheme_=lightTheme;
    if (window_) {
        if (!UpdateTheme()) { lightTheme_=previous; return; }
        RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
    }
}
bool Window::CreateContent() {
    if (!UpdateTheme()) return false;
    body_=CreateWindowExW(WS_EX_CONTROLPARENT,kBodyClass,english_ ? L"Quick guide" : L"빠른 사용 안내",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|WS_CLIPCHILDREN,0,0,1,1,window_,
        reinterpret_cast<HMENU>(100),GetModuleHandleW(nullptr),this);
    if (!body_) return false;
    auto add=[&](const wchar_t* ko,const wchar_t* en,Font font,Surface surface,bool muted=false) {
        HWND child=CreateWindowW(L"STATIC",english_ ? en : ko,WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOPREFIX,
            0,0,1,1,body_,reinterpret_cast<HMENU>(INT_PTR(200+fields_.size())),nullptr,nullptr);
        fields_.push_back({child,font,surface,muted});
    };
    add(L"빠른 사용 안내",L"QUICK GUIDE",Small,Background,true);
    add(L"LLCV",L"LLCV",Title,Background);
    add(version_.c_str(),version_.c_str(),Small,Key);
    add(L"캡처 장치의 영상과 오디오를 저지연으로 재생합니다.",L"Low-latency video and audio playback from capture devices.",Normal,Background,true);
    add(L"키보드 단축키",L"Keyboard shortcuts",Strong,Card);
    for (const auto& shortcut:kShortcuts) {
        add(shortcut.key,shortcut.key,Strong,Key);
        add(shortcut.korean,shortcut.english,Normal,Card);
    }
    add(VideoOnlyHint(false),VideoOnlyHint(true),Small,Card,true);
    add(L"마우스 조작",L"Mouse controls",Strong,Card);
    add(L"휠",L"Scroll wheel",Strong,Card);
    add(L"음량 5%씩 조절\nL/R 카드 위에서는 개별 조절",L"Adjust volume in 5% steps.\nOver L/R cards: adjust each channel.",Normal,Card,true);
    add(L"Shift + 드래그",L"Shift + drag",Strong,Card);
    add(L"스냅 없이 창 이동",L"Move the window without snapping.",Normal,Card,true);
    add(L"스크린샷",L"Screenshots",Strong,Card);
    add(L"F12로 입력 해상도 PNG 저장\nOSD·화면 필터 제외 / HDR은 SDR로 변환",L"F12 saves a source-resolution PNG.\nNo OSD or display filters; HDR to SDR.",Normal,Card,true);
    add(L"저장 위치",L"Save location",Small,Card,true);
    add(L"사진 / LowLatencyCaptureViewer",L"Pictures / LowLatencyCaptureViewer",Small,Card);
    add(L"클립보드 복사: 영상 설정의 스크린샷",L"Clipboard copy: Screenshots in Video settings.",Small,Card,true);
    footer_=CreateWindowW(L"STATIC",english_ ? L"Playback continues · F1 / Esc closes this guide" : L"재생은 계속됩니다 · F1 / Esc로 안내창 닫기",
        WS_CHILD|WS_VISIBLE|SS_NOPREFIX,0,0,1,1,window_,nullptr,nullptr,nullptr);
    close_=CreateWindowW(L"BUTTON",english_ ? L"Close" : L"닫기",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
        0,0,1,1,window_,reinterpret_cast<HMENU>(IDCANCEL),nullptr,nullptr);
    if (!footer_ || !close_ || fields_.size()!=FieldCount) return false;
    for (const auto& field:fields_) if (!field.hwnd) return false;
    ApplyFont(); return true;
}
void Window::Layout() {
    if (layingOut_ || !body_ || fields_.size()!=FieldCount) return;
    layingOut_=true;
    const UINT dpi=GetDpiForWindow(window_);
    auto px=[&](int value) { return Pixels(value,dpi); };
    RECT root{}; GetClientRect(window_,&root);
    const int footerHeight=px(64), margin=px(24), gap=px(16), padding=px(20);
    MoveWindow(body_,0,0,root.right,std::max(1,int(root.bottom)-footerHeight),TRUE);
    MoveWindow(footer_,margin,std::max(0,int(root.bottom)-px(43)),std::max(1,int(root.right)-px(166)),px(38),TRUE);
    MoveWindow(close_,std::max(0,int(root.right)-px(124)),std::max(0,int(root.bottom)-px(48)),px(100),px(34),TRUE);
    HDC dc=GetDC(body_);
    // Showing/hiding the scrollbar changes the client width. Reflow until stable.
    for (int pass=0;pass<3;++pass) {
        RECT client{}; GetClientRect(body_,&client);
        const int width=std::max(px(240),int(client.right)-2*margin);
        const bool columns=client.right>=px(720);
        keys_.clear();
        auto field=[&](unsigned index,int x,int y,int w) {
            auto& value=fields_[index]; wchar_t text[512]{}; GetWindowTextW(value.hwnd,text,512);
            HGDIOBJ old=SelectObject(dc,fonts_[value.font]);
            RECT measure{0,0,std::max(1,w),0};
            DrawTextW(dc,text,-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
            SelectObject(dc,old);
            const int height=std::max(px(16),int(measure.bottom));
            value.rect={x,y,x+w,y+height}; return height;
        };
        field(Tag,margin,px(18),width);
        const int titleBottom=px(43)+field(Name,margin,px(43),columns ? width-px(106) : width);
        const int versionY=columns ? px(47) : titleBottom+px(10);
        const int versionX=columns ? margin+width-px(82) : margin;
        keys_.push_back({versionX,versionY,versionX+px(82),versionY+px(29)});
        field(Version,versionX+px(8),versionY+px(6),px(66));
        const int subtitleY=columns ? titleBottom+px(10) : versionY+px(40);
        const int top=subtitleY+field(Subtitle,margin,subtitleY,width)+px(24);
        const int leftWidth=columns ? (width-gap)*54/100 : width;
        const int rightX=columns ? margin+leftWidth+gap : margin;
        const int rightWidth=columns ? width-leftWidth-gap : width;
        int y=top+padding;
        y+=field(KeyboardTitle,margin+padding,y,leftWidth-2*padding)+px(16);
        for (unsigned i=0;i<kShortcuts.size();++i) {
            const int rowHeight=std::max(px(40),field(FirstKey+2*i+1,margin+padding+px(72),y+px(6),leftWidth-2*padding-px(72))+px(14));
            keys_.push_back({margin+padding,y,margin+padding+px(60),y+px(29)});
            field(FirstKey+2*i,margin+padding+px(10),y+px(5),px(44));
            y+=rowHeight;
        }
        y+=px(6); y+=field(VideoHint,margin+padding,y,leftWidth-2*padding)+padding;
        cards_[0]={margin,top,margin+leftWidth,y};
        const int mouseTop=columns ? top : y+gap;
        y=mouseTop+padding;
        y+=field(MouseTitle,rightX+padding,y,rightWidth-2*padding)+px(16);
        for (unsigned i:{WheelLabel,DragLabel}) {
            y+=field(i,rightX+padding,y,rightWidth-2*padding)+px(4);
            y+=field(i+1,rightX+padding,y,rightWidth-2*padding)+px(14);
        }
        y+=padding-px(14); cards_[1]={rightX,mouseTop,rightX+rightWidth,y};
        const int shotTop=y+gap; y=shotTop+padding;
        y+=field(ShotTitle,rightX+padding,y,rightWidth-2*padding)+px(12);
        y+=field(ShotSummary,rightX+padding,y,rightWidth-2*padding)+px(16);
        y+=field(FolderLabel,rightX+padding,y,rightWidth-2*padding)+px(6);
        y+=field(FolderText,rightX+padding,y,rightWidth-2*padding)+px(12);
        y+=field(ShotOption,rightX+padding,y,rightWidth-2*padding)+padding;
        cards_[2]={rightX,shotTop,rightX+rightWidth,y};
        contentHeight_=std::max(y,int(cards_[0].bottom))+margin;
        scroll_=std::clamp(scroll_,0,std::max(0,contentHeight_-int(client.bottom)));
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,contentHeight_-1,UINT(client.bottom),scroll_,0};
        SetScrollInfo(body_,SB_VERT,&info,TRUE);
        RECT after{}; GetClientRect(body_,&after);
        if (after.right==client.right) break;
    }
    ReleaseDC(body_,dc);
    for (const auto& field:fields_) {
        const RECT& r=field.rect;
        MoveWindow(field.hwnd,r.left,r.top-scroll_,r.right-r.left,r.bottom-r.top,TRUE);
    }
    InvalidateRect(body_,nullptr,TRUE); InvalidateRect(window_,nullptr,TRUE);
    layingOut_=false;
}
void Window::Scroll(int position) {
    RECT r{}; GetClientRect(body_,&r);
    const int next=std::clamp(position,0,std::max(0,contentHeight_-int(r.bottom)));
    if (next!=scroll_) { scroll_=next; Layout(); }
}
void Window::Paint(HDC dc,bool body) {
    RECT r{}; GetClientRect(body ? body_ : window_,&r); FillRect(dc,&r,brushes_[Background]);
    if (!body) {
        RECT line{0,std::max(0,int(r.bottom)-Pixels(64,GetDpiForWindow(window_))),r.right,
                  std::max(0,int(r.bottom)-Pixels(64,GetDpiForWindow(window_)))+1};
        SetDCBrushColor(dc,palette_.kCardEdge);
        FillRect(dc,&line,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        return;
    }
    HPEN pen=CreatePen(PS_SOLID,1,palette_.kCardEdge);
    HGDIOBJ oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,brushes_[Card]);
    const int radius=Pixels(14,GetDpiForWindow(window_));
    for (const auto& card:cards_) RoundRect(dc,card.left,card.top-scroll_,card.right,card.bottom-scroll_,radius,radius);
    SelectObject(dc,brushes_[Key]);
    for (const auto& key:keys_) RoundRect(dc,key.left,key.top-scroll_,key.right,key.bottom-scroll_,radius/2,radius/2);
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(pen);
}
void Window::Toggle(HWND owner,bool english,const wchar_t* version,int show,bool lightTheme) {
    if (window_) { Close(); return; }
    if (!IsWindow(owner)) return;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc=Proc; wc.hInstance=GetModuleHandleW(nullptr); wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName=kClass;
    if (!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return;
    wc.lpfnWndProc=BodyProc; wc.lpszClassName=kBodyClass;
    if (!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return;
    owner_=owner; english_=english; version_=version ? version : L""; scroll_=0; wheelRemainder_=0;
    lightTheme_=lightTheme;
    const UINT dpi=GetDpiForWindow(owner);
    RECT rect{0,0,Pixels(820,dpi),Pixels(680,dpi)};
    constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN;
    if (!AdjustWindowRectExForDpi(&rect,style,FALSE,WS_EX_CONTROLPARENT,dpi)) AdjustWindowRectEx(&rect,style,FALSE,WS_EX_CONTROLPARENT);
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor) &&
        !SystemParametersInfoW(SPI_GETWORKAREA,0,&monitor.rcWork,0))
        monitor.rcWork={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
    const RECT work=monitor.rcWork;
    const int width=std::min(int(rect.right-rect.left),int(work.right-work.left));
    const int height=std::min(int(rect.bottom-rect.top),int(work.bottom-work.top));
    window_=CreateWindowExW(WS_EX_CONTROLPARENT,kClass,english ? L"App information & shortcuts" : L"앱 정보 · 단축키",style,
        work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,owner,nullptr,GetModuleHandleW(nullptr),this);
    if (!window_) return;
    Layout(); ShowWindow(window_,show); if (show!=SW_HIDE) SetFocus(close_);
}
bool Window::ProcessMessage(MSG& m,HWND owner,bool english,const wchar_t* version,bool lightTheme) {
    if (window_) SetLightTheme(lightTheme);
    if (m.message==WM_KEYUP && m.wParam==VK_ESCAPE && dismissEscape_) { dismissEscape_=false; return true; }
    if (m.message==WM_KEYDOWN && m.wParam==VK_ESCAPE && dismissEscape_) {
        if (m.lParam & (LPARAM{1}<<30)) return true;
        dismissEscape_=false;
    }
    if (m.message==WM_KEYDOWN && m.wParam==kHelpKey) {
        if (!(m.lParam & (LPARAM{1}<<30))) Toggle(owner,english,version,SW_SHOWNORMAL,lightTheme);
        return true;
    }
    if (window_ && m.message==WM_KEYDOWN && m.wParam==VK_ESCAPE) { dismissEscape_=true; Close(); return true; }
    if (window_ && (m.hwnd==window_ || IsChild(window_,m.hwnd))) {
        if (m.message==WM_KEYDOWN && m.wParam==kScreenshotKey) return false;
        if (m.message==WM_MOUSEWHEEL) { SendMessageW(body_,m.message,m.wParam,m.lParam); return true; }
        if (!IsDialogMessageW(window_,&m)) { TranslateMessage(&m); DispatchMessageW(&m); }
        return true;
    }
    return false;
}
LRESULT CALLBACK Window::BodyProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* self=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); self->body_=hwnd;
    }
    if (!self) return DefWindowProcW(hwnd,message,wParam,lParam);
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); self->Paint(dc,true); EndPaint(hwnd,&ps); return 0; }
    case WM_PRINTCLIENT: self->Paint(reinterpret_cast<HDC>(wParam),true); return 0;
    case WM_SIZE: self->Layout(); return 0;
    case WM_GETFONT: return reinterpret_cast<LRESULT>(self->fonts_[Normal]);
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN: SetFocus(hwnd); return 0;
    case WM_KEYDOWN: {
        RECT r{}; GetClientRect(hwnd,&r); const int line=Pixels(40,GetDpiForWindow(hwnd));
        switch (wParam) {
        case VK_UP: self->Scroll(self->scroll_-line); return 0;
        case VK_DOWN: self->Scroll(self->scroll_+line); return 0;
        case VK_PRIOR: self->Scroll(self->scroll_-r.bottom); return 0;
        case VK_NEXT: self->Scroll(self->scroll_+r.bottom); return 0;
        case VK_HOME: self->Scroll(0); return 0;
        case VK_END: self->Scroll(self->contentHeight_); return 0;
        } break;
    }
    case WM_MOUSEWHEEL:
        self->wheelRemainder_+=GET_WHEEL_DELTA_WPARAM(wParam);
        self->Scroll(self->scroll_-(self->wheelRemainder_/WHEEL_DELTA)*Pixels(72,GetDpiForWindow(hwnd)));
        self->wheelRemainder_%=WHEEL_DELTA; return 0;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(hwnd,SB_VERT,&info);
        int next=self->scroll_; const int line=Pixels(40,GetDpiForWindow(hwnd));
        switch (LOWORD(wParam)) {
        case SB_LINEUP: next-=line; break; case SB_LINEDOWN: next+=line; break;
        case SB_PAGEUP: next-=int(info.nPage); break; case SB_PAGEDOWN: next+=int(info.nPage); break;
        case SB_TOP: next=0; break; case SB_BOTTOM: next=info.nMax; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: next=info.nTrackPos; break;
        }
        self->Scroll(next); return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc=reinterpret_cast<HDC>(wParam);
        for (const auto& field:self->fields_) if (field.hwnd==reinterpret_cast<HWND>(lParam)) {
            const auto& p=self->palette_;
            const COLORREF bg[]{p.kBackground,p.kCard,p.kControl};
            SetBkColor(dc,bg[field.surface]);
            SetTextColor(dc,field.surface==Key ? p.kAccent : (field.muted ? p.kSecondary : p.kText));
            return reinterpret_cast<LRESULT>(self->brushes_[field.surface]);
        } break;
    }
    }
    return DefWindowProcW(hwnd,message,wParam,lParam);
}
LRESULT CALLBACK Window::Proc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* self=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); self->window_=hwnd;
    }
    if (!self) return DefWindowProcW(hwnd,message,wParam,lParam);
    switch (message) {
    case WM_CREATE: return self->CreateContent() ? 0 : -1;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); self->Paint(dc,false); EndPaint(hwnd,&ps); return 0; }
    case WM_PRINTCLIENT: self->Paint(reinterpret_cast<HDC>(wParam),false); return 0;
    case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(wParam),self->palette_.kBackground);
        SetTextColor(reinterpret_cast<HDC>(wParam),self->palette_.kSecondary);
        return reinterpret_cast<LRESULT>(self->brushes_[Background]);
    case WM_DRAWITEM: {
        const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item->CtlID!=IDCANCEL) break;
        const int saved=SaveDC(item->hDC);
        const auto& p=self->palette_;
        FillRect(item->hDC,&item->rcItem,self->brushes_[Background]);
        SelectObject(item->hDC,GetStockObject(DC_BRUSH));
        SelectObject(item->hDC,GetStockObject(NULL_PEN));
        SetDCBrushColor(item->hDC,item->itemState&ODS_SELECTED ? p.kPrimaryPressed : p.kAccent);
        const int radius=Pixels(14,GetDpiForWindow(hwnd));
        RoundRect(item->hDC,item->rcItem.left,item->rcItem.top,item->rcItem.right,item->rcItem.bottom,radius,radius);
        SetBkMode(item->hDC,TRANSPARENT); SetTextColor(item->hDC,p.kOnAccent);
        HGDIOBJ old=SelectObject(item->hDC,self->fonts_[Strong]);
        RECT text=item->rcItem; if (item->itemState&ODS_SELECTED) OffsetRect(&text,1,1);
        DrawTextW(item->hDC,self->english_ ? L"Close" : L"닫기",-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(item->hDC,old);
        if (item->itemState&ODS_FOCUS) { RECT focus=item->rcItem; InflateRect(&focus,-3,-3); DrawFocusRect(item->hDC,&focus); }
        if (saved) RestoreDC(item->hDC,saved);
        return TRUE;
    }
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: case WM_SYSCOLORCHANGE:
        if (self->UpdateTheme())
            RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
        return 0;
    case WM_SIZE: self->Layout(); return 0;
    case WM_DPICHANGED: {
        self->ApplyFont(); const auto* r=reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        self->Layout(); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize={Pixels(380,GetDpiForWindow(hwnd)),Pixels(300,GetDpiForWindow(hwnd))}; return 0;
    }
    case WM_COMMAND: if (LOWORD(wParam)==IDCANCEL || LOWORD(wParam)==IDOK) { self->Close(); return 0; } break;
    case WM_CLOSE: self->Close(); return 0;
    case WM_NCDESTROY:
        self->window_=self->body_=self->footer_=self->close_=nullptr; self->fields_.clear();
        for (auto& font:self->fonts_) { if (font) DeleteObject(font); font=nullptr; }
        for (auto& brush:self->brushes_) { if (brush) DeleteObject(brush); brush=nullptr; }
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); break;
    }
    return DefWindowProcW(hwnd,message,wParam,lParam);
}
} // namespace llcv::viewer_help
