#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>

namespace llcv::video {

struct PreferredColor {
    bool known = false;
    bool hdr = false;
    uint32_t transfer = 0;
    uint32_t maxLuminance = 0;
    uint32_t referenceLuminance = 0;
    uint32_t targetMaxLuminance = 0;
};

struct HdrOutputState;

class HdrOutput {
public:
    HdrOutput();
    ~HdrOutput();
    HdrOutput(const HdrOutput&) = delete;
    HdrOutput& operator=(const HdrOutput&) = delete;

    static bool Compiled();
    bool Initialize(SDL_Window* window);
    void Shutdown();
    bool Connected() const;
    bool Supported() const;
    bool Active() const;
    std::string Capabilities() const;
    PreferredColor QueryPreferred();
    bool TakePreferredChanged();
    bool Enable();
    void Disable();

private:
    std::unique_ptr<HdrOutputState> state_;
};

}
