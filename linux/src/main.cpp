#include "app/App.h"
#include "app/AppInfo.h"

#include <cstdio>
#include <string_view>

namespace {

void PrintUsage() {
    std::printf(
        "Usage: llcv [options]\n"
        "\n"
        "Low-latency viewer for USB (UVC) HDMI capture devices.\n"
        "\n"
        "Options:\n"
        "  --x11        use the X11 (XWayland) backend for this run\n"
        "  --wayland    use the Wayland backend for this run\n"
        "  --settings   always show the settings screen first\n"
        "  --sdr        disable HDR output and the 10-bit framebuffer for this run\n"
        "  --version    print the version and exit\n"
        "  -h, --help   print this help and exit\n");
}

}

int main(int argc, char** argv) {
    llcv::app::LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--x11") {
            options.backend = llcv::settings::DisplayBackend::X11;
        } else if (argument == "--wayland") {
            options.backend = llcv::settings::DisplayBackend::Wayland;
        } else if (argument == "--settings") {
            options.forceSettings = true;
        } else if (argument == "--sdr") {
            options.forceSdr = true;
        } else if (argument == "--version") {
            std::printf("%s %s\n", llcv::app::kAppName, llcv::app::kVersion);
            return 0;
        } else if (argument == "-h" || argument == "--help") {
            PrintUsage();
            return 0;
        } else {
            std::fprintf(stderr, "llcv: unknown option '%s'\n", argv[i]);
            PrintUsage();
            return 2;
        }
    }
    llcv::app::App app;
    return app.Run(options);
}
