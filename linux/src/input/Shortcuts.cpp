#include "input/Shortcuts.h"

namespace llcv::input {
namespace {

constexpr std::string_view kDefaultLabels[kActionCount] = {"F1", "F2", "F3", "F5", "F11", "F12", "Tab", "Esc", "F9", "M"};

bool IsAsciiAlnum(char value) {
    const unsigned char c = static_cast<unsigned char>(value);
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

char AsciiLower(char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool EqualsIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (AsciiLower(left[i]) != AsciiLower(right[i])) return false;
    }
    return true;
}

bool ConsumePrefix(std::string_view& text, std::string_view prefix) {
    if (text.size() < prefix.size() || !EqualsIgnoreCase(text.substr(0, prefix.size()), prefix)) return false;
    text.remove_prefix(prefix.size());
    return true;
}

bool IsUnused(const Bindings& bindings, size_t index, Binding binding) {
    for (size_t i = 0; i < kActionCount; ++i) {
        if (i != index && bindings[i] == binding) return false;
    }
    return true;
}

std::string KeyName(int key) {
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) return std::string(1, static_cast<char>('A' + key - SDL_SCANCODE_A));
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9) return std::string(1, static_cast<char>('1' + key - SDL_SCANCODE_1));
    if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12) return "F" + std::to_string(key - SDL_SCANCODE_F1 + 1);
    if (key >= SDL_SCANCODE_F13 && key <= SDL_SCANCODE_F24) return "F" + std::to_string(key - SDL_SCANCODE_F13 + 13);
    if (key >= SDL_SCANCODE_KP_1 && key <= SDL_SCANCODE_KP_9) return "Num" + std::to_string(key - SDL_SCANCODE_KP_1 + 1);
    switch (key) {
    case SDL_SCANCODE_0: return "0";
    case SDL_SCANCODE_TAB: return "Tab";
    case SDL_SCANCODE_SPACE: return "Space";
    case SDL_SCANCODE_ESCAPE: return "Esc";
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_BACKSPACE: return "Bksp";
    case SDL_SCANCODE_PAGEUP: return "PgUp";
    case SDL_SCANCODE_PAGEDOWN: return "PgDn";
    case SDL_SCANCODE_HOME: return "Home";
    case SDL_SCANCODE_END: return "End";
    case SDL_SCANCODE_LEFT: return "Left";
    case SDL_SCANCODE_RIGHT: return "Right";
    case SDL_SCANCODE_UP: return "Up";
    case SDL_SCANCODE_DOWN: return "Down";
    case SDL_SCANCODE_INSERT: return "Ins";
    case SDL_SCANCODE_DELETE: return "Del";
    case SDL_SCANCODE_KP_0: return "Num0";
    case SDL_SCANCODE_KP_DIVIDE: return "Num/";
    case SDL_SCANCODE_KP_MULTIPLY: return "Num*";
    case SDL_SCANCODE_KP_MINUS: return "Num-";
    case SDL_SCANCODE_KP_PLUS: return "Num+";
    case SDL_SCANCODE_KP_PERIOD: return "Num.";
    default: return {};
    }
}

}

bool IsModifierKey(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_LGUI:
    case SDL_SCANCODE_RGUI:
    case SDL_SCANCODE_MODE:
        return true;
    default:
        return false;
    }
}

Binding FromKeyEvent(const SDL_KeyboardEvent& key) {
    const SDL_Scancode scancode = key.scancode == SDL_SCANCODE_KP_ENTER ? SDL_SCANCODE_RETURN : key.scancode;
    Binding binding = static_cast<Binding>(scancode) & kScancodeMask;
    if (key.mod & SDL_KMOD_CTRL) binding |= kCtrl;
    if (key.mod & SDL_KMOD_ALT) binding |= kAlt;
    if (key.mod & SDL_KMOD_SHIFT) binding |= kShift;
    if (key.mod & SDL_KMOD_GUI) binding |= kSuper;
    return binding;
}

std::optional<Action> FindAction(const Bindings& bindings, Binding binding) {
    for (size_t i = 0; i < kActionCount; ++i) {
        if (bindings[i] == binding) return static_cast<Action>(i);
    }
    return std::nullopt;
}

bool IsValid(Binding binding) {
    if (binding & ~(kScancodeMask | kCtrl | kAlt | kShift)) return false;
    const int key = static_cast<int>(binding & kScancodeMask);
    if (KeyName(key).empty()) return false;
    const bool ctrl = (binding & kCtrl) != 0;
    const bool alt = (binding & kAlt) != 0;
    if (alt && (key == SDL_SCANCODE_F4 || key == SDL_SCANCODE_SPACE || key == SDL_SCANCODE_TAB ||
                key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_ESCAPE)) {
        return false;
    }
    if (ctrl && key == SDL_SCANCODE_ESCAPE) return false;
    if (ctrl && alt &&
        (key == SDL_SCANCODE_DELETE || key == SDL_SCANCODE_BACKSPACE ||
         (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12))) {
        return false;
    }
    return true;
}

bool IsValid(const Bindings& bindings) {
    for (size_t i = 0; i < kActionCount; ++i) {
        if (!IsValid(bindings[i])) return false;
        for (size_t j = 0; j < i; ++j) {
            if (bindings[i] == bindings[j]) return false;
        }
    }
    return true;
}

std::string Label(Binding binding) {
    std::string label;
    if (binding & kCtrl) label += "Ctrl+";
    if (binding & kAlt) label += "Alt+";
    if (binding & kShift) label += "Shift+";
    const std::string key = KeyName(static_cast<int>(binding & kScancodeMask));
    return label + (key.empty() ? "?" : key);
}

std::optional<Binding> Parse(std::string_view text) {
    Binding modifiers = 0;
    for (;;) {
        if (ConsumePrefix(text, "Ctrl+")) {
            modifiers |= kCtrl;
        } else if (ConsumePrefix(text, "Alt+")) {
            modifiers |= kAlt;
        } else if (ConsumePrefix(text, "Shift+")) {
            modifiers |= kShift;
        } else {
            break;
        }
    }
    for (int key = 0; key < SDL_SCANCODE_COUNT; ++key) {
        const std::string name = KeyName(key);
        if (!name.empty() && EqualsIgnoreCase(name, text)) return modifiers | static_cast<Binding>(key);
    }
    return std::nullopt;
}

std::string ExpandKeyLabels(std::string_view text, const Bindings& bindings) {
    if (bindings == kDefaultBindings) return std::string(text);
    std::string result;
    result.reserve(text.size());
    size_t position = 0;
    while (position < text.size()) {
        bool replaced = false;
        if (position == 0 || !IsAsciiAlnum(text[position - 1])) {
            for (size_t i = 0; i < kActionCount; ++i) {
                const std::string_view token = kDefaultLabels[i];
                const size_t end = position + token.size();
                if (end <= text.size() && text.substr(position, token.size()) == token &&
                    (end == text.size() || !IsAsciiAlnum(text[end]))) {
                    result += Label(bindings[i]);
                    position = end;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) result += text[position++];
    }
    return result;
}

void AssignFreeDefault(Bindings& bindings, Action action) {
    const size_t index = static_cast<size_t>(action);
    const Binding preferred = kDefaultBindings[index];
    if (IsUnused(bindings, index, preferred)) {
        bindings[index] = preferred;
        return;
    }
    const int first = action == Action::Recording ? SDL_SCANCODE_F9 : SDL_SCANCODE_F1;
    for (int key = first; key <= SDL_SCANCODE_F12; ++key) {
        if (key != SDL_SCANCODE_F4 && IsUnused(bindings, index, static_cast<Binding>(key))) {
            bindings[index] = static_cast<Binding>(key);
            return;
        }
    }
    for (int key = SDL_SCANCODE_F13; key <= SDL_SCANCODE_F24; ++key) {
        if (IsUnused(bindings, index, static_cast<Binding>(key))) {
            bindings[index] = static_cast<Binding>(key);
            return;
        }
    }
}

}
