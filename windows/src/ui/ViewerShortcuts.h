#pragma once
#include <windows.h>
#include <array>
#include <string>

namespace llcv::viewer_help {
inline constexpr UINT kHelpKey = VK_F1;
inline constexpr UINT kScreenshotKey = VK_F12;
inline bool IsScreenshotRequest(const MSG& message, bool audioOnly) {
    return message.message==WM_KEYDOWN && message.wParam==kScreenshotKey &&
        !(message.lParam & (LPARAM{1}<<30)) && !audioOnly;
}
struct Shortcut { const wchar_t* key; const wchar_t* korean; const wchar_t* english; bool videoOnly; };
inline constexpr std::array<Shortcut,9> kShortcuts{{
    {L"F1",L"앱 정보 · 단축키",L"App information & shortcuts",false},
    {L"F2",L"설정 열기",L"Open settings",false},
    {L"F3",L"오디오 OSD",L"Audio OSD",true},
    {L"F5",L"Pixel-perfect 크기 복원",L"Restore Pixel-perfect size",true},
    {L"F6",L"NVIDIA VSR 켜기/끄기",L"Toggle NVIDIA VSR",true},
    {L"F11",L"보더리스 전체화면",L"Borderless fullscreen",false},
    {L"F12",L"스크린샷 저장",L"Save screenshot",true},
    {L"Tab",L"실시간 진단",L"Live diagnostics",false},
    {L"Esc",L"전체화면 해제 / 종료",L"Leave fullscreen / exit",false}
}};
inline const wchar_t* VideoOnlyHint(bool english) {
    return english ? L"F3, F5, F6 and F12 are available in video mode."
                   : L"F3 · F5 · F6 · F12는 영상 모드에서 사용합니다.";
}
inline const wchar_t* Shortcuts(bool english, bool alignedColumns = false) {
    static const auto text=[] {
        std::array<std::wstring,4> result;
        for (int aligned=0;aligned<2;++aligned) for (int language=0;language<2;++language) for (const auto& shortcut:kShortcuts) {
            auto& value=result[aligned*2+language];
            if (!value.empty()) value+=L"\r\n";
            value+=shortcut.key; value+=aligned ? L"\t" : L"  "; value+=language ? shortcut.english : shortcut.korean;
            if (shortcut.videoOnly) value+=language ? L" (video mode)" : L" (영상 모드)";
        }
        return result;
    }();
    return text[(alignedColumns ? 2 : 0)+(english ? 1 : 0)].c_str();
}
} // namespace llcv::viewer_help
