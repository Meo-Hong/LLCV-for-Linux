#include "ui/ImGuiPlatform.h"

#include <cfloat>
#include <string>

namespace llcv::ui {
namespace {

std::string g_clipboardText;

const char* GetClipboardText(ImGuiContext*) {
    char* text = SDL_GetClipboardText();
    g_clipboardText = text ? text : "";
    SDL_free(text);
    return g_clipboardText.c_str();
}

void SetClipboardText(ImGuiContext*, const char* text) {
    SDL_SetClipboardText(text);
}

ImGuiKey ToImGuiKey(SDL_Keycode key) {
    if (key >= SDLK_A && key <= SDLK_Z) return static_cast<ImGuiKey>(ImGuiKey_A + (key - SDLK_A));
    if (key >= SDLK_0 && key <= SDLK_9) return static_cast<ImGuiKey>(ImGuiKey_0 + (key - SDLK_0));
    if (key >= SDLK_F1 && key <= SDLK_F12) return static_cast<ImGuiKey>(ImGuiKey_F1 + (key - SDLK_F1));
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
        return static_cast<ImGuiKey>(ImGuiKey_Keypad1 + (key - SDLK_KP_1));
    }
    switch (key) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_INSERT: return ImGuiKey_Insert;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_APOSTROPHE: return ImGuiKey_Apostrophe;
    case SDLK_COMMA: return ImGuiKey_Comma;
    case SDLK_MINUS: return ImGuiKey_Minus;
    case SDLK_PERIOD: return ImGuiKey_Period;
    case SDLK_SLASH: return ImGuiKey_Slash;
    case SDLK_SEMICOLON: return ImGuiKey_Semicolon;
    case SDLK_EQUALS: return ImGuiKey_Equal;
    case SDLK_LEFTBRACKET: return ImGuiKey_LeftBracket;
    case SDLK_BACKSLASH: return ImGuiKey_Backslash;
    case SDLK_RIGHTBRACKET: return ImGuiKey_RightBracket;
    case SDLK_GRAVE: return ImGuiKey_GraveAccent;
    case SDLK_CAPSLOCK: return ImGuiKey_CapsLock;
    case SDLK_SCROLLLOCK: return ImGuiKey_ScrollLock;
    case SDLK_NUMLOCKCLEAR: return ImGuiKey_NumLock;
    case SDLK_PRINTSCREEN: return ImGuiKey_PrintScreen;
    case SDLK_PAUSE: return ImGuiKey_Pause;
    case SDLK_KP_0: return ImGuiKey_Keypad0;
    case SDLK_KP_PERIOD: return ImGuiKey_KeypadDecimal;
    case SDLK_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case SDLK_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case SDLK_KP_MINUS: return ImGuiKey_KeypadSubtract;
    case SDLK_KP_PLUS: return ImGuiKey_KeypadAdd;
    case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_KP_EQUALS: return ImGuiKey_KeypadEqual;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_LGUI: return ImGuiKey_LeftSuper;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    case SDLK_RGUI: return ImGuiKey_RightSuper;
    case SDLK_APPLICATION: return ImGuiKey_Menu;
    default: return ImGuiKey_None;
    }
}

int ToImGuiButton(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return 0;
    case SDL_BUTTON_RIGHT: return 1;
    case SDL_BUTTON_MIDDLE: return 2;
    case SDL_BUTTON_X1: return 3;
    case SDL_BUTTON_X2: return 4;
    default: return -1;
    }
}

SDL_SystemCursor ToSystemCursor(int cursor) {
    switch (cursor) {
    case ImGuiMouseCursor_TextInput: return SDL_SYSTEM_CURSOR_TEXT;
    case ImGuiMouseCursor_ResizeAll: return SDL_SYSTEM_CURSOR_MOVE;
    case ImGuiMouseCursor_ResizeNS: return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case ImGuiMouseCursor_ResizeEW: return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case ImGuiMouseCursor_ResizeNESW: return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    case ImGuiMouseCursor_ResizeNWSE: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case ImGuiMouseCursor_Hand: return SDL_SYSTEM_CURSOR_POINTER;
    case ImGuiMouseCursor_NotAllowed: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    default: return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

}

void ImGuiPlatform::Initialize(SDL_Window* window) {
    window_ = window;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "llcv_sdl3";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetClipboardTextFn = GetClipboardText;
    platformIo.Platform_SetClipboardTextFn = SetClipboardText;
    for (int cursor = 0; cursor < ImGuiMouseCursor_COUNT; ++cursor) {
        cursors_[cursor] = SDL_CreateSystemCursor(ToSystemCursor(cursor));
    }
    lastFrameNs_ = SDL_GetTicksNS();
}

void ImGuiPlatform::Shutdown() {
    if (textInputActive_ && window_) SDL_StopTextInput(window_);
    textInputActive_ = false;
    for (auto& cursor : cursors_) {
        if (cursor) SDL_DestroyCursor(cursor);
        cursor = nullptr;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
    window_ = nullptr;
}

float ImGuiPlatform::PixelDensity() const {
    const float density = window_ ? SDL_GetWindowPixelDensity(window_) : 1.0f;
    return density > 0.0f ? density : 1.0f;
}

void ImGuiPlatform::ProcessEvent(const SDL_Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    const float density = PixelDensity();
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        io.AddMousePosEvent(event.motion.x * density, event.motion.y * density);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        io.AddMouseWheelEvent(-event.wheel.x, event.wheel.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const int button = ToImGuiButton(event.button.button);
        if (button < 0) break;
        io.AddMousePosEvent(event.button.x * density, event.button.y * density);
        io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        io.AddInputCharactersUTF8(event.text.text);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const SDL_Keymod mod = event.key.mod;
        io.AddKeyEvent(ImGuiMod_Ctrl, (mod & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (mod & SDL_KMOD_ALT) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (mod & SDL_KMOD_GUI) != 0);
        const ImGuiKey key = ToImGuiKey(event.key.key);
        if (key != ImGuiKey_None) {
            io.AddKeyEvent(key, event.type == SDL_EVENT_KEY_DOWN);
            io.SetKeyEventNativeData(key, static_cast<int>(event.key.key),
                                     static_cast<int>(event.key.scancode),
                                     static_cast<int>(event.key.scancode));
        }
        break;
    }
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        io.AddFocusEvent(true);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        io.AddFocusEvent(false);
        break;
    default:
        break;
    }
}

void ImGuiPlatform::NewFrame() {
    ImGuiIO& io = ImGui::GetIO();
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
        width = 0;
        height = 0;
    }
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    const uint64_t now = SDL_GetTicksNS();
    const double delta = static_cast<double>(now - lastFrameNs_) / 1.0e9;
    io.DeltaTime = static_cast<float>(delta > 1.0e-5 ? delta : 1.0e-5);
    lastFrameNs_ = now;
    UpdateCursor();
    UpdateTextInput();
}

void ImGuiPlatform::UpdateCursor() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) return;
    const int cursor = ImGui::GetMouseCursor();
    if (cursor == lastCursor_) return;
    lastCursor_ = cursor;
    if (cursor >= 0 && cursor < ImGuiMouseCursor_COUNT && cursors_[cursor]) {
        SDL_SetCursor(cursors_[cursor]);
    } else if (cursors_[ImGuiMouseCursor_Arrow]) {
        SDL_SetCursor(cursors_[ImGuiMouseCursor_Arrow]);
    }
}

void ImGuiPlatform::UpdateTextInput() {
    const bool wanted = ImGui::GetIO().WantTextInput;
    if (wanted == textInputActive_) return;
    if (wanted) {
        SDL_StartTextInput(window_);
    } else {
        SDL_StopTextInput(window_);
    }
    textInputActive_ = wanted;
}

}
