// Synthetic hidden-window output transitions. Readbacks belong to this test,
// never the production resize path. No capture device or desktop mode changes.
static int TestRendererResize() {
    using Microsoft::WRL::ComPtr;
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "resize COM");
    HWND window = CreateWindowExW(0, L"STATIC", L"Hidden resize test", WS_POPUP,
        0, 0, 256, 144, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(window != nullptr, "resize hidden window");
    g_settings.pixelPerfect = false; g_settings.audioOnly = false;
    g_fullscreen = false; g_useScrgbPrototype = false;
    g_osdVisible = false; g_audioOsdVisible = false; g_volumeHudUntilMs = 0;
    double fullInitializationMs = 0, resizeMs = 0;
    unsigned resized = 0, initialized = 0;
    for (const auto mode : {PresentationMode::AllowTearing, PresentationMode::VSync,
                            PresentationMode::Compatibility}) {
        g_settings.presentationMode = mode;
        for (const auto format : {VideoPixelFormat::Nv12, VideoPixelFormat::Yuy2,
                                 VideoPixelFormat::P010}) {
            const bool hdr = format == VideoPixelFormat::P010;
            if (hdr && llcv::presentation::IsCompatibility(mode)) continue;
            for (const auto chroma : {DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020,
                                     DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020}) {
                if (!hdr && chroma == DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020) continue;
                SetWindowPos(window, nullptr, 0, 0, 256, 144,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                g_settings.scalingMode = ScalingMode::Sharp;
                g_vsrMode = llcv::vsr::Mode::Off;
                DirectD3D11Renderer renderer;
                const auto begin = std::chrono::steady_clock::now();
                const HRESULT hr = renderer.initialize(window, 128, 72, 60, format, hdr, {}, chroma);
                if (hr == DXGI_ERROR_UNSUPPORTED || hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
                    std::puts("SKIP: resize GPU format/debug layer unavailable");
                    DestroyWindow(window); CoUninitialize(); return 77;
                }
                Check(hr, "resize initial renderer");
                fullInitializationMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - begin).count(); ++initialized;
                ComPtr<ID3D11Device> device = renderer.device;
                ComPtr<ID3D11Texture2D> upload = renderer.nv12Textures[0];
                ComPtr<ID3D11Texture2D> osd = renderer.osdOverlayTexture;
                ComPtr<ID3D11VertexShader> shader = renderer.overlayVertexShader;
                ComPtr<IDWriteFactory> fonts = renderer.dwriteFactory;
                ComPtr<IDXGISwapChain1> chain = renderer.swapChain;
                const unsigned stride = 128 * (format == VideoPixelFormat::Nv12 ? 1 : 2);
                const unsigned rows = format == VideoPixelFormat::Yuy2 ? 72 : 108;
                std::vector<BYTE> pixels(stride * rows, 128);
                if (hdr) {
                    auto* words = reinterpret_cast<unsigned short*>(pixels.data());
                    std::fill(words, words + pixels.size() / 2, static_cast<unsigned short>(512 << 6));
                    std::fill(words, words + 128 * 72, static_cast<unsigned short>(300 << 6));
                } else if (format == VideoPixelFormat::Nv12) {
                    std::fill(pixels.begin(), pixels.begin() + 128 * 72, BYTE{100});
                } else {
                    for (size_t index = 0; index < pixels.size(); index += 2) pixels[index] = 100;
                }
                auto draw = [&] {
                    renderer.upload(pixels.data(), stride);
                    D3D11_VIDEO_PROCESSOR_STREAM stream{};
                    stream.Enable = TRUE; stream.pInputSurface = renderer.inputViews[renderer.activeUploadSurface];
                    Check(renderer.processVideo(stream), "resize video processing");
                    return Read(renderer, renderer.outputWidth / 2, renderer.outputHeight / 2);
                };
                const auto reference = draw();
                for (const SIZE size : {SIZE{384,216}, SIZE{128,72}, SIZE{96,54},
                                        SIZE{320,180}, SIZE{320,240}, SIZE{320,240}}) {
                    Require(SetWindowPos(window, nullptr, 0, 0, size.cx, size.cy,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), "resize window geometry");
                    g_settings.scalingMode = resized & 1 ? ScalingMode::Smooth : ScalingMode::Sharp;
                    g_settings.pixelPerfect = resized & 1;
                    g_fullscreen = resized & 1; // policy only; never changes desktop/window styles
                    g_vsrMode = resized & 1 ? llcv::vsr::Mode::On : llcv::vsr::Mode::Off;
                    ++g_outputConfigurationGeneration;
                    const auto resizeBegin = std::chrono::steady_clock::now();
                    Check(renderer.resizeOutput(window), "resize output resources");
                    resizeMs += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - resizeBegin).count(); ++resized;
                    Require(renderer.device == device.Get() && renderer.swapChain == chain.Get() &&
                        renderer.nv12Textures[0] == upload.Get() && renderer.osdOverlayTexture == osd.Get() &&
                        renderer.overlayVertexShader == shader.Get() && renderer.dwriteFactory == fonts.Get(),
                        "resize preserves device/upload/shader/font/overlay/swapchain identities");
                    Require(renderer.outputWidth == static_cast<UINT>(size.cx) &&
                        renderer.outputHeight == static_cast<UINT>(size.cy) && !renderer.outputConfigurationChanged(),
                        "resized output dimensions and generation committed");
                    Require(renderer.hdrOutput == hdr && renderer.hdrInputColorSpace == chroma,
                        "resize preserves HDR mode/chroma interpretation");
                    const auto actual = draw();
                    for (unsigned channel = 0; channel < 3; ++channel)
                        Require(std::abs(int(actual[channel]) - int(reference[channel])) <= (hdr ? 4 : 2),
                                "resize preserves SDR/HDR video values");
                    g_osdVisible = true; ++g_overlayGeneration;
                    Check(renderer.drawOverlayQuads(), "cached overlays after resize");
                    g_osdVisible = false;
                    RequireCleanGpu(renderer);
                }
                g_settings.pixelPerfect = false; g_fullscreen = false; g_volumeHudUntilMs = 0;
            }
        }
    }
    std::printf("Resize cache: %u initializations %.3f ms total, %u output-only changes %.3f ms total. "
        "CPU transition work only, not display latency.\n", initialized, fullInitializationMs, resized, resizeMs);
    DestroyWindow(window); CoUninitialize(); return 0;
}
