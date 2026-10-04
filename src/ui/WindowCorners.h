#pragma once

#include <windows.h>
#include <dwmapi.h>

namespace llcv::window_corners {

constexpr DWM_WINDOW_CORNER_PREFERENCE Preference(bool enabled, bool fullscreen,
                                                   bool maximized) noexcept {
    return enabled && !fullscreen && !maximized ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
}

// UI-thread only. A DWM hint, not a region/alpha mask or a video filter.
// Unsupported systems keep their existing frame; don't retry on every resize.
class State {
public:
    using Setter = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    void Reset() noexcept { owner_ = nullptr; }
    HRESULT Apply(HWND window, bool enabled, bool fullscreen, bool maximized, bool borderless,
                  Setter setter = DwmSetWindowAttribute) noexcept {
        if (!window) return E_INVALIDARG;
        const auto preference = Preference(enabled, fullscreen, maximized);
        // Hide DWM's contrasting outline only on borderless windows. Regular
        // captioned windows retain the system frame in either corner mode.
        // Both styles use the same corner preference above.
        const COLORREF border = enabled && borderless ? DWMWA_COLOR_NONE : DWMWA_COLOR_DEFAULT;
        if (owner_ == window && preference_ == preference && border_ == border) return result_;
        owner_ = window;
        preference_ = preference;
        border_ = border;
        const HRESULT cornerResult = setter(window, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
        const HRESULT borderResult = setter(window, DWMWA_BORDER_COLOR, &border, sizeof(border));
        result_ = FAILED(cornerResult) ? cornerResult : borderResult;
        return result_;
    }
private:
    HWND owner_ = nullptr;
    DWM_WINDOW_CORNER_PREFERENCE preference_ = DWMWCP_DEFAULT;
    COLORREF border_ = DWMWA_COLOR_DEFAULT;
    HRESULT result_ = S_OK;
};

} // namespace llcv::window_corners
