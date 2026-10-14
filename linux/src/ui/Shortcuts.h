#pragma once

#include "input/Shortcuts.h"

namespace llcv::ui {

struct Shortcut {
    input::Action action;
    const char* korean;
    const char* english;
    bool videoOnly;
};

inline constexpr Shortcut kShortcuts[] = {
    {input::Action::Help, "앱 정보 · 단축키", "App information & shortcuts", false},
    {input::Action::Settings, "설정 열기", "Open settings", false},
    {input::Action::AudioOsd, "오디오 OSD", "Audio OSD", true},
    {input::Action::PixelPerfect, "Pixel-perfect 크기 복원", "Restore Pixel-perfect size", true},
    {input::Action::Fullscreen, "전체화면 (Alt+Enter)", "Fullscreen (Alt+Enter)", false},
    {input::Action::Screenshot, "스크린샷 저장", "Save screenshot", true},
    {input::Action::Diagnostics, "실시간 진단", "Live diagnostics", false},
    {input::Action::Exit, "전체화면 해제 / 종료", "Leave fullscreen / exit", false},
};

struct FixedShortcut {
    const char* key;
    const char* korean;
    const char* english;
};

inline constexpr FixedShortcut kMenuShortcut = {"Alt", "메뉴 막대 표시 / 숨기기", "Show / hide the menu bar"};

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
