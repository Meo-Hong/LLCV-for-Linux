#pragma once
#include "ViewerShortcuts.h"
#include "AppPalette.h"
#include <array>
#include <string>
#include <vector>

namespace llcv::viewer_help {
// UI-thread-only modeless owned window. No renderer/capture/settings ownership.
class Window {
public:
    ~Window();
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    void Toggle(HWND owner, bool english, const wchar_t* version, int show = SW_SHOWNORMAL,
                bool lightTheme = false);
    // Call before viewer shortcut/wheel routing. Dismissal consumes key repeat.
    bool ProcessMessage(MSG& message, HWND owner, bool english, const wchar_t* version,
                        bool lightTheme = false);
    void SetLightTheme(bool lightTheme);
    HWND Handle() const { return window_; }
    void Close();
private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK BodyProc(HWND, UINT, WPARAM, LPARAM);
    void Layout();
    void ApplyFont();
    bool CreateContent();
    bool UpdateTheme();
    void Paint(HDC dc, bool body);
    void Scroll(int position);
    enum Font { Normal, Strong, Title, Small, FontCount };
    enum Surface { Background, Card, Key, SurfaceCount };
    struct Field { HWND hwnd; Font font; Surface surface; bool muted; RECT rect{}; };
    HWND window_ = nullptr, owner_ = nullptr, body_ = nullptr, close_ = nullptr, footer_ = nullptr;
    std::vector<Field> fields_;
    std::array<HFONT, FontCount> fonts_{};
    std::array<HBRUSH, SurfaceCount> brushes_{};
    std::array<RECT, 3> cards_{};
    std::vector<RECT> keys_;
    std::wstring version_;
    int scroll_ = 0, contentHeight_ = 0, wheelRemainder_ = 0;
    bool layingOut_ = false, highContrast_ = false;
    bool english_ = false, dismissEscape_ = false;
    bool lightTheme_ = false;
    ui::Palette palette_;
};
} // namespace llcv::viewer_help
