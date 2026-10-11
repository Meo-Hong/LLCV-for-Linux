#pragma once

#include <windows.h>
#include <commctrl.h>
#include <vector>
#include "AppPalette.h"

namespace llcv::settings_ui {
struct SettingsControls;

// UI-thread-only, paint-only decoration. Native control classes, messages,
// accessibility, keyboard navigation and selection semantics stay intact.
class SettingsTheme final {
public:
    SettingsTheme() = default;
    ~SettingsTheme();
    SettingsTheme(const SettingsTheme&) = delete;
    SettingsTheme& operator=(const SettingsTheme&) = delete;

    bool Attach(HWND owner, const SettingsControls* controls);
    // Match the DPI used by ApplySettingsFont/LayoutSettingsControls. Zero uses
    // the owning window's DPI; explicit DPI also permits offscreen layout QA.
    void RefreshControls(UINT layoutDpi = 0);
    void Detach();
    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);
    bool HighContrast() const { return highContrast_; }

private:
    using Palette = ui::Palette;
    Palette palette_;
    struct Child { HWND hwnd; bool combo; bool hover; bool label; };
    static LRESULT CALLBACK ChildProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static BOOL CALLBACK AttachChild(HWND, LPARAM);
    void UpdateSystemTheme();
    void PaintBackground(HDC);
    void PaintControl(HWND, HDC, bool combo, bool hover);
    void PaintDisabledLabel(HWND, HDC);
    void PaintBrand(HWND, HDC);
    void PaintComboItem(const DRAWITEMSTRUCT&);
    void PaintNavigation(const DRAWITEMSTRUCT&);
    bool IsCardControl(HWND) const;
    void PaintCards(HDC);
    bool IsSecondary(HWND) const;
    bool IsHeading(HWND) const;
    int Pixels(int dip) const;

    HWND owner_ = nullptr;
    const SettingsControls* controls_ = nullptr; // borrowed until Detach
    HBRUSH background_ = nullptr;
    HBRUSH surface_ = nullptr;
    HBRUSH sidebar_ = nullptr;
    HBRUSH card_ = nullptr;
    bool highContrast_ = false;
    UINT dpi_ = 96;
    bool bufferedPaintInitialized_ = false;
    std::vector<Child> children_;
};
} // namespace llcv::settings_ui
