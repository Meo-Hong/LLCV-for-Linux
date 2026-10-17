#pragma once

#include <cstdint>

namespace llcv::exclusive_presentation {

// Published by the window owner; only the renderer changes DXGI fullscreen state.
struct Inputs {
    bool selected = false;
    bool fullscreen = false;
    bool active = false;
    bool minimized = false;
    bool blocked = false; // Settings, help, or a modal dialog owns focus.
    bool closing = false;
};

constexpr bool Wanted(const Inputs& state) noexcept {
    return state.selected && state.fullscreen && state.active &&
           !state.minimized && !state.blocked && !state.closing;
}

// A failed transition stays latched until the UI publishes a new revision.
// Recreated swapchains call Reset so the new resource gets one fresh attempt.
class AttemptGate {
public:
    bool Begin(std::uint64_t revision, bool wanted, bool currentExclusive) noexcept {
        if (wanted == currentExclusive) return false;
        if (attempted_ && revision == revision_ && wanted == wanted_) return false;
        attempted_ = true;
        revision_ = revision;
        wanted_ = wanted;
        return true;
    }

    void Reset() noexcept {
        attempted_ = false;
    }

private:
    std::uint64_t revision_ = 0;
    bool wanted_ = false;
    bool attempted_ = false;
};

} // namespace llcv::exclusive_presentation
