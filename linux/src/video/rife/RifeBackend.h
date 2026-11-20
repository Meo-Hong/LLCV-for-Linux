#pragma once

#include "video/GlApi.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace llcv::rife_vulkan {

inline constexpr int kWidth = 1920;
inline constexpr int kHeight = 1080;

struct GlDevice {
    std::array<uint8_t, 16> deviceUuid{};
    std::array<uint8_t, 16> driverUuid{};
};

bool QueryGlDevice(GlDevice& device, std::string& reason);

struct HudStats {
    uint64_t jobs = 0;
    uint64_t active = 0;
    double meanCore = 0.0;
};

class Backend {
public:
    static std::unique_ptr<Backend> Prepare(const GlDevice& gl, const std::filesystem::path& models, bool hud,
                                            bool chroma);
    ~Backend();
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    void AttachGl();
    GLuint InputTexture() const;
    GLuint OutputTexture() const;
    bool Free();
    void Enqueue(int64_t arrivalUs);
    bool WaitUntil(int64_t deadlineUs);
    void AcquireOutput();
    bool TakeGpuMs(double& ms);
    bool Hud(HudStats& stats);
    const std::string& DeviceName() const;

private:
    Backend();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
