#include "video/FrameInterpolator.h"

namespace llcv::video {

struct FrameInterpolator::Impl {};

FrameInterpolator::FrameInterpolator() = default;

FrameInterpolator::~FrameInterpolator() = default;

bool FrameInterpolator::Built() {
    return false;
}

bool FrameInterpolator::ModelsAvailable() {
    return false;
}

void FrameInterpolator::Configure(bool, bool) {}

void FrameInterpolator::Reset() {
    status_ = InterpolationStatus::Off;
}

void FrameInterpolator::MarkUnsupported() {
    status_ = InterpolationStatus::Unsupported;
}

void FrameInterpolator::MarkOff() {
    status_ = InterpolationStatus::Off;
}

GLuint FrameInterpolator::Submit(GLuint, int, int, double, int64_t) {
    status_ = InterpolationStatus::Failed;
    detail_ = "this build has no frame interpolation";
    return 0;
}

void FrameInterpolator::ScheduleOriginal(double) {}

void FrameInterpolator::WaitOriginal() {}

InterpolationStats FrameInterpolator::Stats() const {
    return {};
}

std::string FrameInterpolator::DeviceName() const {
    return {};
}

}
