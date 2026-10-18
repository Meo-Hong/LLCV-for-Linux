#pragma once

#include <SDL3/SDL_events.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace llcv::input {

enum class Action : size_t {
    Help, Settings, AudioOsd, PixelPerfect, Fullscreen, Screenshot, Diagnostics, Exit, Recording, Mute, Count
};

inline constexpr size_t kActionCount = static_cast<size_t>(Action::Count);

using Binding = uint32_t;
using Bindings = std::array<Binding, kActionCount>;

inline constexpr Binding kScancodeMask = 0xffff;
inline constexpr Binding kCtrl = 0x10000;
inline constexpr Binding kAlt = 0x20000;
inline constexpr Binding kShift = 0x40000;
inline constexpr Binding kSuper = 0x80000;

inline constexpr Bindings kDefaultBindings{
    SDL_SCANCODE_F1,  SDL_SCANCODE_F2,  SDL_SCANCODE_F3,  SDL_SCANCODE_F5,
    SDL_SCANCODE_F11, SDL_SCANCODE_F12, SDL_SCANCODE_TAB, SDL_SCANCODE_ESCAPE,
    SDL_SCANCODE_F9,  SDL_SCANCODE_M,
};

inline constexpr const char* kActionNames[kActionCount] = {
    "Help", "Settings", "AudioOsd", "PixelPerfect", "Fullscreen", "Screenshot", "Diagnostics", "Exit", "Recording", "Mute",
};

inline Binding BindingFor(const Bindings& bindings, Action action) {
    return bindings[static_cast<size_t>(action)];
}

bool IsModifierKey(SDL_Scancode scancode);
Binding FromKeyEvent(const SDL_KeyboardEvent& key);
std::optional<Action> FindAction(const Bindings& bindings, Binding binding);
bool IsValid(Binding binding);
bool IsValid(const Bindings& bindings);
std::string Label(Binding binding);
std::optional<Binding> Parse(std::string_view text);
std::string ExpandKeyLabels(std::string_view text, const Bindings& bindings);
void AssignFreeDefault(Bindings& bindings, Action action);

}
