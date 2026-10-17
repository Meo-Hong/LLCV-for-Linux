#include "video/VideoPlacement.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool valid, const char* message) {
    if (!valid) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    using namespace llcv;
    using P = settings::FullscreenVideoPosition;
    auto rect = [](int sw, int sh, int ow, int oh, bool fullscreen, int dw, int dh, P position) {
        return video_placement::Destination(sw, sh, ow, oh, fullscreen, dw, dh, position);
    };
    const RECT centered{640, 360, 3200, 1800};
    auto actual = rect(1920, 1080, 3840, 2160, true, 2560, 1440, P::Center);
    Check(EqualRect(&actual, &centered), "FHD capture displays as QHD inside 4K fullscreen");
    unsigned cases = 0;
    for (auto source : {SIZE{1920,1080}, SIZE{2560,1440}, SIZE{3840,2160}, SIZE{1280,720}, SIZE{1440,1080}})
    for (auto output : {SIZE{3840,2160}, SIZE{2560,1440}, SIZE{1920,1080}, SIZE{3440,1440}, SIZE{1080,1920}})
    for (auto display : {SIZE{1280,720}, SIZE{1920,1080}, SIZE{2560,1440}, SIZE{3840,2160}})
    for (int i = 0; i < settings::kFullscreenVideoPositionCount; ++i) {
        const auto p = settings::FullscreenPositionFromIndex(i);
        const auto r = rect(source.cx, source.cy, output.cx, output.cy, true, display.cx, display.cy, p);
        const LONG w = r.right - r.left, h = r.bottom - r.top;
        Check(w > 0 && h > 0 && r.left >= 0 && r.top >= 0 &&
              r.right <= output.cx && r.bottom <= output.cy, "never crops or exceeds the output");
        Check(w <= display.cx && h <= display.cy, "never exceeds selected display size");
        Check(std::abs(static_cast<double>(w) * source.cy / source.cx - h) <= 1.5,
              "source aspect ratio preserved within pixel rounding");
        if (p == P::TopLeft || p == P::Left || p == P::BottomLeft) Check(r.left == 0, "left anchor");
        else if (p == P::TopRight || p == P::Right || p == P::BottomRight) Check(r.right == output.cx, "right anchor");
        else Check(r.left == (output.cx - w) / 2, "horizontal center");
        if (p == P::TopLeft || p == P::Top || p == P::TopRight) Check(r.top == 0, "top anchor");
        else if (p == P::BottomLeft || p == P::Bottom || p == P::BottomRight) Check(r.bottom == output.cy, "bottom anchor");
        else Check(r.top == (output.cy - h) / 2, "vertical center");
        const auto windowed = rect(source.cx, source.cy, output.cx, output.cy, false, display.cx, display.cy, p);
        const auto reference = rect(source.cx, source.cy, output.cx, output.cy, false, 1, 1, P::Center);
        Check(EqualRect(&windowed, &reference), "windowed fit ignores fullscreen position and selected-size cap");
        ++cases;
    }
    Check(settings::FullscreenPositionFromIndex(-1) == P::Center &&
          settings::FullscreenPositionFromIndex(9) == P::Center, "invalid positions default to center");
    actual = rect(0, 1080, 3840, 2160, true, 1920, 1080, P::Center);
    Check(IsRectEmpty(&actual), "invalid source produces no rectangle");
    std::printf("PASS: %u fullscreen size/position/aspect/windowed regression cases.\n", cases);
}
