#include "SettingsFonts.h"
#include <windows.h>

namespace llcv::settings_ui {
namespace {
struct PrivateFonts {
    HANDLE fonts[3]{};
    PrivateFonts() {
        const HMODULE module = GetModuleHandleW(nullptr);
        constexpr int resourceIds[] = {9001, 9002, 9004}; // 9003 holds the license.
        for (int i = 0; i < 3; ++i) {
            const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceIds[i]), RT_RCDATA);
            if (!resource) continue;
            const HGLOBAL loaded = LoadResource(module, resource);
            void* bytes = loaded ? LockResource(loaded) : nullptr;
            DWORD count = 0;
            if (bytes) fonts[i] = AddFontMemResourceEx(bytes, SizeofResource(module, resource), nullptr, &count);
        }
    }
    ~PrivateFonts() { for (HANDLE font : fonts) if (font) RemoveFontMemResourceEx(font); }
};
}
const wchar_t* SettingsFontFamily(int weight, bool english) {
    // TrueType-outline resources keep GDI ClearType available at small sizes.
    // No system installation, disk extraction, network access or per-frame work.
    static const PrivateFonts fonts;
    if (fonts.fonts[0] && fonts.fonts[1] && fonts.fonts[2]) {
        if (weight >= FW_SEMIBOLD) return L"Pretendard SemiBold";
        return weight >= FW_MEDIUM ? L"Pretendard Medium" : L"Pretendard";
    }
    return english ? L"Segoe UI" : L"Malgun Gothic";
}
}
