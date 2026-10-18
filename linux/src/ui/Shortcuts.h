#pragma once

#include "input/Shortcuts.h"
#include "ui/Text.h"

namespace llcv::ui {

struct Shortcut {
    input::Action action;
    const char* text;
    bool videoOnly;
};

inline constexpr Shortcut kShortcuts[] = {
    {input::Action::Help, N_("App information & shortcuts"), false},
    {input::Action::Settings, N_("Open settings"), false},
    {input::Action::AudioOsd, N_("Audio OSD"), true},
    {input::Action::PixelPerfect, N_("Restore Pixel-perfect size"), true},
    {input::Action::Fullscreen, N_("Fullscreen (Alt+Enter)"), false},
    {input::Action::Screenshot, N_("Save screenshot"), true},
    {input::Action::Diagnostics, N_("Live diagnostics"), false},
    {input::Action::Exit, N_("Leave fullscreen / exit"), false},
    {input::Action::Recording, N_("Start / stop recording"), true},
    {input::Action::Mute, N_("Toggle playback mute"), false},
};

struct FixedShortcut {
    const char* key;
    const char* text;
};

inline constexpr FixedShortcut kMenuShortcut = {"Alt", N_("Show / hide the menu bar")};

struct MouseHint {
    const char* gesture;
    const char* text;
};

inline constexpr MouseHint kMouseHints[] = {
    {N_("Wheel"), N_("Volume ±5%")},
    {N_("Double-click"), N_("Toggle fullscreen")},
};

}
