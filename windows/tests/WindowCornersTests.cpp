#include "ui/WindowCorners.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static void Check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static int calls = 0;
static int borderCalls = 0;
static COLORREF lastBorder = DWMWA_COLOR_DEFAULT;
static HRESULT result = S_OK;
static DWM_WINDOW_CORNER_PREFERENCE last = DWMWCP_DEFAULT;
static int nativeBorders = 0;
static HRESULT WINAPI NativeSet(HWND window, DWORD attribute, LPCVOID value, DWORD size) {
    const HRESULT hr = DwmSetWindowAttribute(window, attribute, value, size);
    if (attribute == DWMWA_BORDER_COLOR && SUCCEEDED(hr)) ++nativeBorders;
    return hr;
}
static HRESULT WINAPI Set(HWND, DWORD attribute, LPCVOID value, DWORD size) {
    if (attribute == DWMWA_BORDER_COLOR) {
        Check(size == sizeof(lastBorder), "border COLORREF ABI");
        lastBorder = *static_cast<const COLORREF*>(value);
        ++borderCalls; return result;
    }
    Check(attribute == DWMWA_WINDOW_CORNER_PREFERENCE && size == sizeof(last), "correct DWM ABI");
    last = *static_cast<const DWM_WINDOW_CORNER_PREFERENCE*>(value);
    ++calls; return result;
}

int main() {
    using namespace llcv::window_corners;
    for (bool enabled : {false, true}) for (bool full : {false, true}) for (bool max : {false, true})
        Check(Preference(enabled, full, max) == (enabled && !full && !max ? DWMWCP_ROUND : DWMWCP_DONOTROUND),
            "rounded only when enabled and windowed/restored");
    State state;
    HWND fake = reinterpret_cast<HWND>(1);
    Check(state.Apply(nullptr, true, false, false, true, Set) == E_INVALIDARG && calls == 0, "null handle inert");
    state.Apply(fake, true, false, false, true, Set);
    Check(calls == 1 && last == DWMWCP_ROUND, "initial standard radius");
    Check(borderCalls == 1 && lastBorder == DWMWA_COLOR_NONE, "rounded window has no DWM outline");
    for (int i=0; i<1000; ++i) state.Apply(fake, true, false, false, true, Set);
    Check(calls == 1, "resize never repeats identical DWM calls");
    Check(borderCalls == 1, "resize never repeats border calls");
    state.Apply(fake, true, true, false, true, Set);
    Check(calls == 2 && last == DWMWCP_DONOTROUND, "fullscreen square");
    state.Apply(fake, true, false, true, true, Set);
    Check(calls == 2, "maximized already square");
    state.Apply(fake, true, false, false, true, Set);
    Check(calls == 3 && last == DWMWCP_ROUND, "restore rounds again");
    state.Apply(fake, false, false, false, true, Set);
    Check(calls == 4 && last == DWMWCP_DONOTROUND, "explicit OFF overrides OS default");
    Check(lastBorder == DWMWA_COLOR_DEFAULT, "OFF restores system border policy");
    result = E_INVALIDARG; state.Reset();
    for (int i=0; i<1000; ++i) Check(state.Apply(fake, true, false, false, true, Set) == E_INVALIDARG, "unsupported result retained");
    Check(calls == 5, "unsupported OS no retry storm or clipping fallback");
    state.Reset(); state.Apply(fake, true, false, false, true, Set);
    Check(calls == 6, "theme/composition or handle reuse retries once");
    Check(borderCalls == 6, "unsupported border API is cached too");
    result = S_OK;
    state.Apply(fake, true, false, true, true, Set);
    Check(last == DWMWCP_DONOTROUND && lastBorder == DWMWA_COLOR_NONE, "maximized retains no-outline intent");
    const int maximizedCalls = calls;
    state.Apply(fake, false, false, true, true, Set);
    Check(calls == maximizedCalls + 1 && last == DWMWCP_DONOTROUND && lastBorder == DWMWA_COLOR_DEFAULT,
        "border changes even when corner preference stays square");

    // Changing frame style must invalidate the border policy even when the
    // radius stays identical. Regular frames always retain system borders.
    state.Apply(fake, true, false, false, true, Set);
    const int beforeStyle = calls;
    state.Apply(fake, true, false, false, false, Set);
    Check(calls == beforeStyle + 1 && last == DWMWCP_ROUND &&
        lastBorder == DWMWA_COLOR_DEFAULT, "normal window restores border with same radius");
    for (int i=0; i<1000; ++i) state.Apply(fake, true, false, false, false, Set);
    Check(calls == beforeStyle + 1, "normal frame resize is cached");
    state.Apply(fake, true, false, false, true, Set);
    Check(calls == beforeStyle + 2 && last == DWMWCP_ROUND &&
        lastBorder == DWMWA_COLOR_NONE, "borderless restores no-outline with same radius");
    for (bool enabled : {false, true}) for (bool full : {false, true}) for (bool max : {false, true}) {
        state.Apply(fake, enabled, full, max, false, Set);
        Check(lastBorder == DWMWA_COLOR_DEFAULT, "normal border retained in every corner state");
    }

    // Real hidden HWNDs, no capture, audio, rendering, visible UI or settings writes.
    int native = 0;
    for (DWORD style : {DWORD{WS_OVERLAPPEDWINDOW}, DWORD{WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX}, DWORD{WS_POPUP}}) {
        HWND window = CreateWindowExW(0, L"STATIC", L"Corner regression", style,
            0, 0, 640, 360, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(window != nullptr, "hidden native window");
        RECT before{}, after{}; GetClientRect(window, &before);
        const auto originalStyle = GetWindowLongPtrW(window, GWL_STYLE);
        State actual;
        for (bool enabled : {true, false, true}) for (bool full : {false, true, false}) {
            const auto hr = actual.Apply(window, enabled, full, false, (style & WS_POPUP) != 0, NativeSet);
            if (SUCCEEDED(hr)) {
                DWM_WINDOW_CORNER_PREFERENCE value = DWMWCP_DEFAULT;
                Check(SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &value, sizeof(value))) &&
                    value == Preference(enabled, full, false), "real DWM preference readback");
                // BORDER_COLOR is documented for SetWindowAttribute only.
                // GetWindowAttribute returns E_INVALIDARG on this host; do not
                // mistake an unsupported readback for a failed border update.
                Check(nativeBorders > 0, "native DWM accepted border attributes");
                ++native;
            }
            GetClientRect(window, &after);
            Check(EqualRect(&before, &after) && GetWindowLongPtrW(window, GWL_STYLE) == originalStyle,
                "native/borderless client pixels and styles unchanged");
            HRGN region = CreateRectRgn(0,0,0,0);
            Check(GetWindowRgn(window, region) == ERROR, "no window-region clipping fallback");
            DeleteObject(region);
        }
        DestroyWindow(window);
    }
    std::printf("PASS: corner/border policy/cache/failure/geometry; native corner readbacks=%d, border setters=%d (not pixel certification).\n", native, nativeBorders);
    return 0;
}
