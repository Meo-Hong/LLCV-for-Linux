#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>

// Private opt-in experiment. No SDK DLLs, CPU readbacks, extra video surfaces,
// future frames, or application frame queue are used by this path.
namespace llcv::vsr {
enum class Mode { Disabled, Off, On };
enum class State { Untouched, Bypassed, Off, Requested, Rejected, Unknown };
constexpr bool Eligible(UINT vendor, DXGI_FORMAT format, bool hdr,
                        UINT width, UINT height, UINT outputWidth, UINT outputHeight) {
    // Native-resolution de-artifacting uses the same processor request as
    // upscaling. Never request it while either axis is being downscaled.
    return vendor == 0x10de && format == DXGI_FORMAT_NV12 && !hdr &&
        width > 0 && height > 0 && outputWidth >= width && outputHeight >= height;
}

// S_OK only means the driver accepted the request, NOT confirmed VSR activation.
// ABI also used by Chromium ui/gl/swap_chain_presenter.cc ToggleNvidiaVpSuperResolution.
HRESULT SetRequest(ID3D11VideoContext*, ID3D11VideoProcessor*, bool enable);
const wchar_t* StateName(State);
struct SupportProbe {
    enum class Status { Unavailable, NonNvidia, RequestSupported };
    Status status = Status::Unavailable;
    HRESULT result = E_FAIL;
    std::wstring adapter;
};
// Explicit settings action only. Isolated processor; never changes driver profiles,
// opens a capture device, or certifies the NVIDIA global setting/actual activation.
SupportProbe ProbeSupport();

struct Distribution {
    // Bounded histogram: no allocations or sorting on the render thread.
    // Bucket 1000 is overflow (>=100 ms); max/mean remain exact.
    std::array<uint64_t, 1001> bins{};
    uint64_t count = 0;
    double total = 0, maximum = 0;
    void Add(double ms);
    double Percentile(double fraction) const;
    double Mean() const { return count ? total / static_cast<double>(count) : 0; }
};

class Timing {
public:
    using Clock = std::chrono::steady_clock;
    struct Results {
        Distribution cpuBlt, captureToPresent;
    };
    HRESULT Initialize(ID3D11Device*, unsigned warmupFrames);
    void Reset();
    void Begin(ID3D11DeviceContext*);
    void End(ID3D11DeviceContext*, bool success);
    void AddCaptureToPresent(double ms);
    Results Snapshot() const;
private:
    Results results_{};
    Clock::time_point cpuStart_{};
    unsigned warmup_ = 0;
    bool ready_ = false, sample_ = false;
};
} // namespace llcv::vsr
