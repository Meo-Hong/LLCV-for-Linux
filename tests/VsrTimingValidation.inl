// Test-only timing validation. Never included by the production executable.
// Compare bare VP timestamps, output-dependent timestamps and a serialized
// readback reference. No capture devices, visible windows or profile writes.
static int ValidateVsrTiming(int argc, char** argv) {
    using Microsoft::WRL::ComPtr;
    using Clock = std::chrono::steady_clock;
    const auto ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
    bool reverse = false, use720p = false;
    for (int i = 2; i < argc; ++i) {
        reverse |= std::string(argv[i]) == "--reverse";
        use720p |= std::string(argv[i]) == "--720p";
    }
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "timing validation COM");
    const int width = use720p ? 1280 : 1920, height = use720p ? 720 : 1080;
    constexpr int frames = 120, warmup = 40;
    HWND window = CreateWindowW(L"STATIC", L"Hidden timing validation", WS_POPUP,
        0, 0, width * 2, height * 2, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(window != nullptr, "timing validation window");
    g_suppressSettingsSave = true;
    g_settings.pixelPerfect = false; g_settings.scalingMode = ScalingMode::Smooth;
    g_settings.presentationMode = PresentationMode::AllowTearing;
    g_osdVisible = false; g_audioOsdVisible = false; g_volumeHudUntilMs = 0;
    std::array<std::vector<BYTE>, 8> patterns;
    for (size_t frame = 0; frame < patterns.size(); ++frame) {
        auto& data = patterns[frame];
        data.resize(static_cast<size_t>(width) * height * 3 / 2, 128);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const int shifted = x + static_cast<int>(frame) * 3;
            data[static_cast<size_t>(y) * width + x] = static_cast<BYTE>(
                16 + ((shifted / 3 + y / 5 + ((shifted / 40 + y / 40) % 2) * 90) % 220));
        }
    }
    std::printf("VALIDATION input=%dx%d output=%dx%d reverse=%d, 60fps, frames=%d warmup=%d; "
        "NO Present, NOT display latency; NVIDIA quality unchanged/unverified\n",
        width, height, width * 2, height * 2, reverse, frames, warmup);
    // Rotate method order between runs to expose power/load/order effects.
    for (int methodOrder = 0; methodOrder < 5; ++methodOrder) {
        const int method = reverse ? 4 - methodOrder : methodOrder;
        const char* name = method == 0 ? "reference" : method == 1 ? "serialized_queries" :
            method == 2 ? "async_queries" : method == 3 ? "async_no_copy" : "async_control";
        const bool instrumented = method != 0 && method != 4, asynchronous = method >= 2;
        const bool copyOutput = method <= 2;
        for (int phase = 0; phase < 6; ++phase) {
            const bool on = static_cast<bool>(phase % 2) != reverse;
            g_vsrMode = on ? llcv::vsr::Mode::On : llcv::vsr::Mode::Off;
            DirectD3D11Renderer r;
            Check(r.initialize(window, width, height, 60, VideoPixelFormat::Nv12), "timing renderer");
            if (!r.vsrEligible || r.vsrState != (on ? llcv::vsr::State::Requested : llcv::vsr::State::Off)) {
                std::puts("SKIP: requested NVIDIA VSR route unavailable");
                r.reset(); DestroyWindow(window); CoUninitialize(); return 77;
            }
            struct Slot {
                ComPtr<ID3D11Query> disjoint, start, beforeVp, afterVp, afterCopy;
                ComPtr<ID3D11Texture2D> pixel;
                bool pending = false, record = false;
                int frame = 0;
                Clock::time_point submitted{};
            };
            std::array<Slot, 6> slots;
            const auto query = [&](D3D11_QUERY type, ComPtr<ID3D11Query>& result) {
                const D3D11_QUERY_DESC desc{type, 0};
                Check(r.device->CreateQuery(&desc, &result), "timing query creation");
            };
            for (auto& slot : slots) {
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
                desc.Usage = asynchronous ? D3D11_USAGE_DEFAULT : D3D11_USAGE_STAGING;
                desc.CPUAccessFlags = asynchronous ? 0 : D3D11_CPU_ACCESS_READ;
                if (copyOutput) Check(r.device->CreateTexture2D(&desc, nullptr, &slot.pixel), "dependent output pixel");
                if (instrumented) {
                    query(D3D11_QUERY_TIMESTAMP_DISJOINT, slot.disjoint);
                    query(D3D11_QUERY_TIMESTAMP, slot.start);
                    query(D3D11_QUERY_TIMESTAMP, slot.beforeVp);
                    query(D3D11_QUERY_TIMESTAMP, slot.afterVp);
                    query(D3D11_QUERY_TIMESTAMP, slot.afterCopy);
                }
            }
            llcv::vsr::Distribution cpuBlt, cpuSubmit, completion, bareGpu, dependentGpu, totalGpu, observedAge, queryCpu;
            unsigned dropped = 0, measuredDropped = 0, disjoints = 0, lateFrames = 0, queryCalls = 0;
            const auto poll = [&] {
                const auto pollingStart = Clock::now();
                for (auto& slot : slots) {
                    if (!slot.pending) continue;
                    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT result{};
                    ++queryCalls;
                    const HRESULT ready = r.context->GetData(slot.disjoint.Get(), &result, sizeof(result),
                        D3D11_ASYNC_GETDATA_DONOTFLUSH);
                    Check(ready, "asynchronous query poll");
                    if (ready != S_OK) continue; // never wait/spin for an in-flight frame
                    if (result.Disjoint || !result.Frequency) {
                        ++disjoints; slot.pending = false; continue;
                    }
                    UINT64 time[4]{};
                    ID3D11Query* stamps[] = {slot.start.Get(), slot.beforeVp.Get(), slot.afterVp.Get(), slot.afterCopy.Get()};
                    bool allReady = true;
                    for (unsigned index = 0; index < 4; ++index) {
                        ++queryCalls;
                        const HRESULT hr = r.context->GetData(stamps[index], &time[index], sizeof(UINT64),
                            D3D11_ASYNC_GETDATA_DONOTFLUSH);
                        Check(hr, "timestamp read"); allReady &= hr == S_OK;
                    }
                    if (!allReady) continue;
                    Require(time[0] <= time[1] && time[1] <= time[2] && time[2] <= time[3], "monotonic GPU timestamps");
                    if (slot.record) {
                        const double factor = 1000.0 / static_cast<double>(result.Frequency);
                        bareGpu.Add((time[2] - time[1]) * factor);
                        dependentGpu.Add((time[3] - time[1]) * factor);
                        totalGpu.Add((time[3] - time[0]) * factor);
                        observedAge.Add(ms(Clock::now() - slot.submitted));
                    }
                    slot.pending = false;
                }
                queryCpu.Add(ms(Clock::now() - pollingStart));
            };
            auto next = Clock::now();
            for (int frame = 0; frame < frames; ++frame) {
                if (instrumented) poll(); // one bounded pass per frame
                auto& slot = slots[frame % slots.size()];
                const bool measure = instrumented && !slot.pending;
                if (instrumented && !measure) {
                    ++dropped;
                    if (frame >= warmup) ++measuredDropped;
                }
                const auto start = Clock::now();
                if (measure) {
                    r.context->Begin(slot.disjoint.Get()); r.context->End(slot.start.Get());
                }
                r.upload(patterns[frame % patterns.size()].data(), width);
                if (measure) r.context->End(slot.beforeVp.Get());
                D3D11_VIDEO_PROCESSOR_STREAM stream{}; stream.Enable = TRUE;
                stream.pInputSurface = r.inputViews[r.activeUploadSurface];
                const auto before = Clock::now();
                Check(r.videoContext->VideoProcessorBlt(r.processor, r.outputView, 0, 1, &stream), "timing VP");
                const double cpu = ms(Clock::now() - before);
                if (measure) r.context->End(slot.afterVp.Get());
                // Dependency forces the sampled pixel to derive from this VP
                // output, without a CPU readback in the async candidate.
                if (copyOutput && (!instrumented || measure)) {
                    const D3D11_BOX box{0, 0, 0, 1, 1, 1};
                    r.context->CopySubresourceRegion(slot.pixel.Get(), 0, 0, 0, 0, r.backBuffer, 0, &box);
                }
                if (measure) {
                    r.context->End(slot.afterCopy.Get()); r.context->End(slot.disjoint.Get());
                    slot.pending = true; slot.record = frame >= warmup;
                    slot.submitted = start; slot.frame = frame;
                }
                // Hidden test has no Present to submit work. Identical explicit
                // test-only Flush in every method; never added to the viewer.
                r.context->Flush();
                const double submit = ms(Clock::now() - start);
                if (!asynchronous) {
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    Check(r.context->Map(slot.pixel.Get(), 0, D3D11_MAP_READ, 0, &mapped), "reference completion");
                    r.context->Unmap(slot.pixel.Get(), 0);
                    if (frame >= warmup) completion.Add(ms(Clock::now() - start));
                }
                if (frame >= warmup) { cpuBlt.Add(cpu); cpuSubmit.Add(submit); }
                next += std::chrono::microseconds(16667);
                if (Clock::now() > next && frame >= warmup) ++lateFrames;
                std::this_thread::sleep_until(next);
            }
            const auto drainStart = Clock::now();
            const uint64_t hash = OutputHash(r); // outside timed frames; verifies changed ON/OFF output
            const double finalReadback = ms(Clock::now() - drainStart);
            if (instrumented) {
                const auto deadline = Clock::now() + std::chrono::milliseconds(500);
                bool pending = true;
                while (pending && Clock::now() < deadline) {
                    poll(); pending = false;
                    for (const auto& slot : slots) pending |= slot.pending;
                    if (pending) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                Require(!pending, "bounded test-only query drain");
            }
            std::printf("RESULT method=%s phase=%d mode=%s hash=%016llX n=%llu "
                "cpu_blt=%.4f cpu_submit=%.4f serialized=%.4f gpu_bare=%.4f gpu_dependency=%.4f "
                "gpu_total=%.4f observed_age=%.4f poll_cpu=%.4f gpu_p95=%.2f "
                "dropped=%u measured_dropped=%u disjoint=%u late=%u query_calls=%u final_readback=%.4f feature_level=%X\n",
                name, phase, on ? "on" : "off", hash, instrumented ? totalGpu.count : cpuSubmit.count,
                cpuBlt.Mean(), cpuSubmit.Mean(), completion.Mean(), bareGpu.Mean(), dependentGpu.Mean(),
                totalGpu.Mean(), observedAge.Mean(), queryCpu.Mean(), totalGpu.Percentile(.95),
                dropped, measuredDropped, disjoints, lateFrames, queryCalls, finalReadback,
                static_cast<unsigned>(r.device->GetFeatureLevel()));
            std::fflush(stdout);
            Require((instrumented ? totalGpu.count : cpuSubmit.count) >= 70, "enough valid validation samples");
        }
    }
    DestroyWindow(window); CoUninitialize();
    std::puts("VALIDATION COMPLETE: raw timing evidence only, not a latency certification.");
    return 0;
}
