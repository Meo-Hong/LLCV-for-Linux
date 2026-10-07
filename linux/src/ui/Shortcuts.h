#pragma once

namespace llcv::ui {

struct Shortcut {
    const char* key;
    const char* korean;
    const char* english;
    bool videoOnly;
};

inline constexpr Shortcut kShortcuts[] = {
    {"Alt", "메뉴 막대 표시 / 숨기기", "Show / hide the menu bar", false},
    {"F1", "앱 정보 · 단축키", "App information & shortcuts", false},
    {"F2", "설정 열기", "Open settings", false},
    {"F3", "오디오 OSD", "Audio OSD", true},
    {"F5", "Pixel-perfect 크기 복원", "Restore Pixel-perfect size", true},
    {"F11", "전체화면 (Alt+Enter)", "Fullscreen (Alt+Enter)", false},
    {"F12", "스크린샷 저장", "Save screenshot", true},
    {"Tab", "실시간 진단", "Live diagnostics", false},
    {"Esc", "전체화면 해제 / 종료", "Leave fullscreen / exit", false},
};

struct MouseHint {
    const char* koreanGesture;
    const char* englishGesture;
    const char* korean;
    const char* english;
};

inline constexpr MouseHint kMouseHints[] = {
    {"휠", "Wheel", "음량 ±5%", "Volume ±5%"},
    {"더블 클릭", "Double-click", "전체화면 전환", "Toggle fullscreen"},
};

}
