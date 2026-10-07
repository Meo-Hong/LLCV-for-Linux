#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>
#include <cstdint>

namespace llcv::ui {

class ImGuiPlatform {
public:
    void Initialize(SDL_Window* window);
    void Shutdown();
    void ProcessEvent(const SDL_Event& event);
    void NewFrame();

private:
    float PixelDensity() const;
    void UpdateCursor();
    void UpdateTextInput();

    SDL_Window* window_ = nullptr;
    uint64_t lastFrameNs_ = 0;
    std::array<SDL_Cursor*, ImGuiMouseCursor_COUNT> cursors_{};
    int lastCursor_ = -2;
    bool textInputActive_ = false;
};

}
