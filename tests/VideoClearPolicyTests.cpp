#include "video/VideoClearPolicy.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using llcv::video_clear::CanSkipBorderClear;
using llcv::video_clear::State;
unsigned checks = 0;

void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

State Valid() {
    State state;
    state.outputWidth = 3840;
    state.outputHeight = 2160;
    state.outputFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    state.videoProcessorPath = true;
    state.outputTargetEnabled = true;
    state.outputTarget = {0, 0, 3840, 2160};
    state.sourceWidth = 1920;
    state.sourceHeight = 1080;
    state.source = {0, 0, 1920, 1080};
    state.destination = {960, 540, 2880, 1620};
    state.singleOpaqueVideoStream = true;
    state.opaqueOutput = true;
    state.backgroundKnown = true;
    state.backgroundIsYuv = false;
    state.backgroundA = 1.0f;
    return state;
}

void TestRoutesAndFormats() {
    Require(!CanSkipBorderClear({}), "uninitialized renderer retains the clear");
    Require(CanSkipBorderClear(Valid()), "SDR opaque black full VP target covers centered video and bars");
    for (unsigned mask = 0; mask < 256; ++mask) {
        auto state = Valid();
        state.hdrOutput = (mask & 1) != 0;
        state.scrgbOutput = (mask & 2) != 0;
        state.videoProcessorPath = (mask & 4) == 0;
        state.outputTargetEnabled = (mask & 8) == 0;
        state.singleOpaqueVideoStream = (mask & 16) == 0;
        state.opaqueOutput = (mask & 32) == 0;
        state.backgroundKnown = (mask & 64) == 0;
        state.backgroundIsYuv = (mask & 128) != 0;
        Require(CanSkipBorderClear(state) == (mask == 0),
            "every route, opacity and background guard is required independently");
    }
    for (const auto format : {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_NV12,
             DXGI_FORMAT_P010, DXGI_FORMAT_YUY2, DXGI_FORMAT_R10G10B10A2_UNORM,
             DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM,
             DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}) {
        auto state = Valid();
        state.outputFormat = format;
        Require(!CanSkipBorderClear(state), "unvalidated SDR, YUV, HDR and scRGB formats retain the clear");
    }
}

void TestBackground() {
    const std::array<float, 7> values{0.000001f, -0.000001f, 1.0f, -1.0f,
        (std::numeric_limits<float>::infinity)(),
        -(std::numeric_limits<float>::infinity)(),
        (std::numeric_limits<float>::quiet_NaN)()};
    for (const float value : values) {
        for (float State::* channel : {&State::backgroundR, &State::backgroundG,
                                       &State::backgroundB}) {
            auto state = Valid();
            state.*channel = value;
            Require(!CanSkipBorderClear(state), "black is exact: near-black, nonfinite and nonblack values retain clear");
        }
    }
    for (const float alpha : {0.0f, 0.5f, 0.999999f, 1.000001f,
             (std::numeric_limits<float>::quiet_NaN)()}) {
        auto state = Valid();
        state.backgroundA = alpha;
        Require(!CanSkipBorderClear(state), "background must replace prior pixels with fully opaque black");
    }
    auto negativeZero = Valid();
    negativeZero.backgroundR = negativeZero.backgroundG = negativeZero.backgroundB = -0.0f;
    Require(CanSkipBorderClear(negativeZero), "negative zero represents the same black pixels");
}

void TestGeometry() {
    for (const RECT target : {RECT{1, 0, 3840, 2160}, RECT{0, 1, 3840, 2160},
             RECT{0, 0, 3839, 2160}, RECT{0, 0, 3840, 2159},
             RECT{-1, 0, 3840, 2160}, RECT{0, 0, 3841, 2160},
             RECT{0, 0, 3840, 2161}, RECT{0, 0, 0, 0}}) {
        auto state = Valid();
        state.outputTarget = target;
        Require(!CanSkipBorderClear(state), "partial, offset, oversize or empty target cannot overwrite old border OSD");
    }
    for (const RECT rectangle : {RECT{-1, 0, 100, 100}, RECT{0, -1, 100, 100},
             RECT{100, 0, 100, 100}, RECT{0, 100, 100, 100},
             RECT{100, 0, 99, 100}, RECT{0, 100, 100, 99}}) {
        auto state = Valid();
        state.destination = rectangle;
        Require(!CanSkipBorderClear(state), "invalid destination retains clear");
        state = Valid();
        state.source = rectangle;
        Require(!CanSkipBorderClear(state), "invalid source retains clear");
    }
    auto state = Valid();
    state.source.right = 1921;
    Require(!CanSkipBorderClear(state), "source cannot exceed input surface");
    state = Valid();
    state.destination.right = 3841;
    Require(!CanSkipBorderClear(state), "destination cannot exceed output surface");
    for (std::uint32_t State::* extent : {&State::outputWidth, &State::outputHeight,
                                          &State::sourceWidth, &State::sourceHeight}) {
        state = Valid();
        state.*extent = 0;
        Require(!CanSkipBorderClear(state), "zero extent retains clear during resize or recreation");
        state.*extent = (std::numeric_limits<std::uint32_t>::max)();
        Require(!CanSkipBorderClear(state), "extent that cannot fit RECT coordinates retains clear");
    }
    for (const auto dimensions : {std::array<LONG, 2>{1, 1}, {3, 5}, {1920, 1080},
                                   {2560, 1440}, {3440, 1440}, {7680, 4320}}) {
        state = Valid();
        state.outputWidth = state.sourceWidth = static_cast<std::uint32_t>(dimensions[0]);
        state.outputHeight = state.sourceHeight = static_cast<std::uint32_t>(dimensions[1]);
        state.outputTarget = state.source = state.destination = {0, 0, dimensions[0], dimensions[1]};
        Require(CanSkipBorderClear(state), "complete opaque video covers smallest, odd, normal and ultrawide targets");
    }
    state = Valid();
    state.destination = {0, 360, 3840, 1800};
    Require(CanSkipBorderClear(state), "letterboxed video uses VP black background for top and bottom bars");
    state.destination = {480, 0, 3360, 2160};
    Require(CanSkipBorderClear(state), "pillarboxed video uses VP black background for side bars");
    state.source = {1, 1, 1919, 1079};
    Require(CanSkipBorderClear(state), "valid source cropping does not expose old output pixels");
}
} // namespace

int main() {
    TestRoutesAndFormats();
    TestBackground();
    TestGeometry();
    std::printf("PASS: %u VideoClearPolicy checks\n", checks);
}
