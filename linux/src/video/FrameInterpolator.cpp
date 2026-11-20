#include "video/FrameInterpolator.h"

#include "diagnostics/Logger.h"
#include "platform/Paths.h"
#include "video/rife/RifeBackend.h"
#include "video/rife/RifeHistoryPolicy.h"

#include <SDL3/SDL_video.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <future>
#include <thread>
#include <vector>

namespace llcv::video {
namespace {

int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t HalfFrameUs(double fps) {
    return fps > 0.0 ? static_cast<int64_t>(std::llround(500000.0 / fps)) : 0;
}

}

struct FrameInterpolator::Impl {
    bool hud = true;
    bool chroma = true;
    std::unique_ptr<rife_vulkan::Backend> backend;
    std::future<std::unique_ptr<rife_vulkan::Backend>> preparing;
    std::vector<std::future<std::unique_ptr<rife_vulkan::Backend>>> retired;
    bool failed = false;
    bool history = false;
    int64_t previousArrival = 0;
    int64_t originalAtUs = 0;
    InterpolationStats stats;
    std::string device;
    PFNGLCOPYIMAGESUBDATAPROC copy = nullptr;

    void Harvest() {
        double ms = 0.0;
        if (backend && backend->TakeGpuMs(ms) && ms >= 0.0) {
            ++stats.gpuJobs;
            stats.gpuMsSum += ms;
            stats.gpuMsMax = std::max(stats.gpuMsMax, ms);
        }
    }

    void Collect() {
        std::erase_if(retired, [](auto& pending) {
            return pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        });
    }

    void Drop() {
        if (preparing.valid()) retired.push_back(std::move(preparing));
        Collect();
        backend.reset();
        history = false;
        previousArrival = 0;
    }
};

FrameInterpolator::FrameInterpolator() : impl_(std::make_unique<Impl>()) {}

FrameInterpolator::~FrameInterpolator() = default;

bool FrameInterpolator::Built() {
    return true;
}

bool FrameInterpolator::ModelsAvailable() {
    return !platform::FindInterpolationModels().empty();
}

void FrameInterpolator::Configure(bool hud, bool chroma) {
    if (impl_->hud == hud && impl_->chroma == chroma) return;
    impl_->hud = hud;
    impl_->chroma = chroma;
    if (impl_->backend || impl_->preparing.valid()) {
        diagnostics::Log("[interpolation] HUD protection changed; rebuilding RIFE");
        impl_->Drop();
        if (status_ != InterpolationStatus::Off) status_ = InterpolationStatus::Pending;
    }
}

void FrameInterpolator::Reset() {
    if (impl_->backend) {
        const auto stats = Stats();
        diagnostics::Log("[interpolation] session: pairs=%llu generated=%llu late=%llu busy=%llu GPU mean %.2f ms max "
                         "%.2f ms",
                         static_cast<unsigned long long>(stats.pairs), static_cast<unsigned long long>(stats.generated),
                         static_cast<unsigned long long>(stats.late), static_cast<unsigned long long>(stats.busy),
                         stats.gpuJobs ? stats.gpuMsSum / static_cast<double>(stats.gpuJobs) : 0.0, stats.gpuMsMax);
    }
    impl_->Drop();
    impl_->failed = false;
    impl_->stats = {};
    status_ = InterpolationStatus::Off;
    detail_.clear();
}

void FrameInterpolator::MarkUnsupported() {
    impl_->history = false;
    if (impl_->failed) {
        impl_->failed = false;
        detail_.clear();
    }
    status_ = InterpolationStatus::Unsupported;
}

void FrameInterpolator::MarkOff() {
    impl_->history = false;
    if (impl_->failed) {
        impl_->failed = false;
        detail_.clear();
    }
    status_ = InterpolationStatus::Off;
}

GLuint FrameInterpolator::Submit(GLuint source, int width, int height, double fps, int64_t arrivalUs) {
    auto& s = *impl_;
    s.Collect();
    if (s.failed) {
        status_ = InterpolationStatus::Failed;
        return 0;
    }
    if (width != kInterpolationWidth || height != kInterpolationHeight || !InterpolationRateSupported(fps)) {
        MarkUnsupported();
        return 0;
    }
    const auto fail = [&](const std::string& reason) {
        diagnostics::Log("[interpolation] RIFE unavailable, showing original frames: %s", reason.c_str());
        detail_ = reason;
        s.failed = true;
        s.backend.reset();
        s.history = false;
        status_ = InterpolationStatus::Failed;
    };
    if (!s.backend) {
        if (!s.preparing.valid()) {
            rife_vulkan::GlDevice gl;
            std::string reason;
            if (!rife_vulkan::QueryGlDevice(gl, reason)) {
                fail(reason);
                return 0;
            }
            const auto models = platform::FindInterpolationModels();
            if (models.empty()) {
                fail("RIFE model files not found");
                return 0;
            }
            if (!s.copy) {
                s.copy = reinterpret_cast<PFNGLCOPYIMAGESUBDATAPROC>(SDL_GL_GetProcAddress("glCopyImageSubData"));
            }
            if (!s.copy) {
                fail("glCopyImageSubData missing");
                return 0;
            }
            const bool hud = s.hud;
            const bool chroma = s.chroma;
            diagnostics::Log("[interpolation] preparing RIFE Vulkan Fast (HUD protection %s) from %s",
                             hud ? (chroma ? "luma and color" : "luma") : "off", models.c_str());
            s.preparing = std::async(std::launch::async, [gl, models, hud, chroma] {
                return rife_vulkan::Backend::Prepare(gl, models, hud, hud && chroma);
            });
            status_ = InterpolationStatus::Pending;
            return 0;
        }
        if (s.preparing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            status_ = InterpolationStatus::Pending;
            return 0;
        }
        try {
            s.backend = s.preparing.get();
            s.backend->AttachGl();
            s.device = s.backend->DeviceName();
            diagnostics::Log("[interpolation] RIFE Vulkan Fast ready on %s (1920x1080, OpenGL shared textures)",
                             s.device.c_str());
        } catch (const std::exception& error) {
            fail(error.what());
            return 0;
        }
        s.history = false;
        s.previousArrival = 0;
    }
    try {
        if (!s.backend->Free()) {
            s.history = false;
            ++s.stats.busy;
            status_ = InterpolationStatus::Late;
            return 0;
        }
        s.Harvest();
        const auto step = rife_test::PlanInputHistory(s.previousArrival, arrivalUs,
                                                      static_cast<int>(std::lround(fps)), s.history);
        s.previousArrival = arrivalUs;
        s.backend->AcquireOutput();
        s.copy(source, GL_TEXTURE_2D, 0, 0, 0, 0, s.backend->InputTexture(), GL_TEXTURE_2D, 0, 0, 0, 0,
               kInterpolationWidth, kInterpolationHeight, 1);
        s.backend->Enqueue(arrivalUs);
        if (!step.pairReady) {
            s.history = true;
            status_ = InterpolationStatus::Pending;
            return 0;
        }
        s.history = true;
        ++s.stats.pairs;
        if (!s.backend->WaitUntil(arrivalUs + HalfFrameUs(fps))) {
            ++s.stats.late;
            status_ = InterpolationStatus::Late;
            return 0;
        }
        s.backend->AcquireOutput();
        ++s.stats.generated;
        status_ = InterpolationStatus::Active;
        return s.backend->OutputTexture();
    } catch (const std::exception& error) {
        fail(error.what());
        return 0;
    }
}

void FrameInterpolator::ScheduleOriginal(double fps) {
    impl_->originalAtUs = NowUs() + HalfFrameUs(fps);
}

void FrameInterpolator::WaitOriginal() {
    const int64_t remaining = impl_->originalAtUs - NowUs();
    if (remaining > 0) std::this_thread::sleep_for(std::chrono::microseconds(remaining));
}

InterpolationStats FrameInterpolator::Stats() const {
    InterpolationStats stats = impl_->stats;
    if (impl_->backend) {
        rife_vulkan::HudStats hud;
        if (impl_->backend->Hud(hud)) {
            stats.hudJobs = hud.jobs;
            stats.hudActive = hud.active;
        }
    }
    return stats;
}

std::string FrameInterpolator::DeviceName() const {
    return impl_->device;
}

}
