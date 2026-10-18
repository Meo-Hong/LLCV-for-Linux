#include "capture/V4l2Capture.h"

#include "capture/HardwareToneMapping.h"
#include "diagnostics/Logger.h"
#include "platform/Clock.h"
#include "platform/Strings.h"
#include "ui/Text.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_thread.h>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace llcv::capture {
namespace {

constexpr uint32_t kRequestedBuffers = 4;

int Ioctl(int fd, unsigned long request, void* argument) {
    int result = 0;
    do {
        result = ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

std::string FourccText(uint32_t fourcc) {
    std::string text(4, ' ');
    for (int i = 0; i < 4; ++i) text[i] = static_cast<char>((fourcc >> (8 * i)) & 0xFF);
    return text;
}

std::string DeviceError(const char* action, int error) {
    if (error == EBUSY) {
        return platform::Format("%s: %s (EBUSY)", action,
                                ui::T("Another application is using the capture device"));
    }
    return platform::Format("%s: %s", action, std::strerror(error));
}

uint64_t TimestampNs(const v4l2_buffer& buffer, uint64_t fallback) {
    const uint32_t type = buffer.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK;
    if (type != V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) return fallback;
    const uint64_t value = static_cast<uint64_t>(buffer.timestamp.tv_sec) * 1000000000ull +
                           static_cast<uint64_t>(buffer.timestamp.tv_usec) * 1000ull;
    return value ? value : fallback;
}

using RateCheck = bool (*)(const FrameRate&);

bool ConfirmRequestedRate(int fd, bool rateSet, int rateError, const v4l2_streamparm& requested, const char* label,
                          RateCheck matches, FrameRate& rate, std::string& error) {
    if (!rateSet) {
        error = platform::Format("%s rejected: VIDIOC_S_PARM failed (%s)", label, std::strerror(rateError));
        return false;
    }
    if (!(requested.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
        error = platform::Format("%s rejected: the driver does not support frame-rate requests (no V4L2_CAP_TIMEPERFRAME)",
                                 label);
        return false;
    }
    const auto& reported = requested.parm.capture.timeperframe;
    if (!reported.numerator || !reported.denominator) {
        error = platform::Format("%s rejected: the driver reported no frame interval", label);
        return false;
    }
    rate = {reported.numerator, reported.denominator};
    v4l2_streamparm active{};
    active.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (Ioctl(fd, VIDIOC_G_PARM, &active) == 0 && active.parm.capture.timeperframe.numerator &&
        active.parm.capture.timeperframe.denominator) {
        rate = {active.parm.capture.timeperframe.numerator, active.parm.capture.timeperframe.denominator};
    }
    diagnostics::Log("[capture] %s request: driver reports %u/%u s per frame (%s fps)", label, rate.numerator,
                     rate.denominator, FormatRate(rate).c_str());
    if (!matches(rate)) {
        error = platform::Format("%s rejected: the driver set %s fps", label, FormatRate(rate).c_str());
        return false;
    }
    return true;
}

}

V4l2Capture::~V4l2Capture() {
    Stop();
}

bool V4l2Capture::Start(const CaptureConfig& config, FrameMailbox& mailbox,
                        std::function<void()> onFrame, std::string& error) {
    Stop();
    config_ = config;
    mailbox_ = &mailbox;
    onFrame_ = std::move(onFrame);
    mailbox_->Reset();
    counters_.frames = 0;
    counters_.replaced = 0;
    counters_.skippedInQueue = 0;
    counters_.driverDropped = 0;
    counters_.corrupt = 0;
    counters_.lastFrameNs = 0;
    counters_.processingNsTotal = 0;
    counters_.processingSamples = 0;
    stopping_ = false;

    wakeFd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wakeFd_ < 0) {
        error = DeviceError("eventfd", errno);
        return false;
    }
    if (!OpenDevice(error)) {
        close(wakeFd_);
        wakeFd_ = -1;
        return false;
    }
    state_ = CaptureState::Streaming;
    SetStatus({});
    thread_ = std::thread(&V4l2Capture::Run, this);
    return true;
}

void V4l2Capture::Stop() {
    if (thread_.joinable()) {
        stopping_ = true;
        const uint64_t one = 1;
        if (write(wakeFd_, &one, sizeof(one)) < 0) {
            diagnostics::Log("[capture] wake write failed: %s", std::strerror(errno));
        }
        thread_.join();
    }
    CloseDevice();
    if (wakeFd_ >= 0) {
        close(wakeFd_);
        wakeFd_ = -1;
    }
    state_ = CaptureState::Stopped;
}

NegotiatedFormat V4l2Capture::Negotiated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return negotiated_;
}

std::string V4l2Capture::StatusMessage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void V4l2Capture::SetStatus(std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = std::move(message);
}

bool V4l2Capture::OpenDevice(std::string& error) {
    fd_ = open(config_.devicePath.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        error = DeviceError(config_.devicePath.c_str(), errno);
        return false;
    }

    const uint32_t fourcc = FourccFor(config_.format);
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = static_cast<uint32_t>(config_.width);
    format.fmt.pix.height = static_cast<uint32_t>(config_.height);
    format.fmt.pix.pixelformat = fourcc;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    if (Ioctl(fd_, VIDIOC_S_FMT, &format) < 0) {
        error = DeviceError("VIDIOC_S_FMT", errno);
        CloseDevice();
        return false;
    }
    if (format.fmt.pix.pixelformat != fourcc ||
        format.fmt.pix.width != static_cast<uint32_t>(config_.width) ||
        format.fmt.pix.height != static_cast<uint32_t>(config_.height)) {
        error = platform::Format("capture mode rejected: requested %dx%d %s, device chose %ux%u %s",
                                 config_.width, config_.height, FourccText(fourcc).c_str(),
                                 format.fmt.pix.width, format.fmt.pix.height,
                                 FourccText(format.fmt.pix.pixelformat).c_str());
        CloseDevice();
        return false;
    }
    bytesPerLine_ = format.fmt.pix.bytesperline;
    if (bytesPerLine_ == 0) {
        switch (config_.format) {
        case PixelFormat::Nv12: bytesPerLine_ = static_cast<uint32_t>(config_.width); break;
        case PixelFormat::Yuyv: bytesPerLine_ = static_cast<uint32_t>(config_.width) * 2; break;
        case PixelFormat::Bgr24: bytesPerLine_ = static_cast<uint32_t>(config_.width) * 3; break;
        case PixelFormat::P010: bytesPerLine_ = static_cast<uint32_t>(config_.width) * 2; break;
        case PixelFormat::Mjpeg: break;
        }
    }

    ConfigureHardwareToneMapping(fd_, config_.devicePath, config_.format,
                                 config_.hdrInput == video::HdrInput::ForceHdr10);

    NegotiatedFormat negotiated;
    negotiated.format = config_.format;
    negotiated.width = config_.width;
    negotiated.height = config_.height;
    negotiated.rate = config_.rate;
    video::ColorInput colorInput;
    colorInput.colorspace = format.fmt.pix.colorspace;
    colorInput.ycbcrEncoding = format.fmt.pix.ycbcr_enc;
    colorInput.quantization = format.fmt.pix.quantization;
    colorInput.transferFunction = format.fmt.pix.xfer_func;
    colorInput.jpeg = config_.format == PixelFormat::Mjpeg;
    colorInput.rgb = config_.format == PixelFormat::Bgr24;
    colorInput.tenBit = config_.format == PixelFormat::P010;
    colorInput.width = config_.width;
    colorInput.height = config_.height;
    negotiated.color = video::ResolveV4l2Color(colorInput, config_.colorOverride, config_.hdrInput);

    v4l2_streamparm parameters{};
    parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parameters.parm.capture.timeperframe.numerator = config_.rate.numerator;
    parameters.parm.capture.timeperframe.denominator = config_.rate.denominator;
    const bool rateSet = Ioctl(fd_, VIDIOC_S_PARM, &parameters) == 0;
    const int rateError = rateSet ? 0 : errno;
    if (rateSet && parameters.parm.capture.timeperframe.numerator &&
        parameters.parm.capture.timeperframe.denominator) {
        negotiated.rate = {parameters.parm.capture.timeperframe.numerator,
                           parameters.parm.capture.timeperframe.denominator};
    } else {
        diagnostics::Log("[capture] VIDIOC_S_PARM failed: %s", std::strerror(rateSet ? EINVAL : rateError));
    }
    if (config_.capture40Fps && !ConfirmRequestedRate(fd_, rateSet, rateError, parameters, "40 fps", IsFortyFps,
                                                      negotiated.rate, error)) {
        CloseDevice();
        return false;
    }
    if (!config_.capture40Fps && config_.directRate &&
        !ConfirmRequestedRate(fd_, rateSet, rateError, parameters, "Direct 30 fps", IsThirtyFps, negotiated.rate,
                              error)) {
        CloseDevice();
        return false;
    }

    v4l2_requestbuffers request{};
    request.count = kRequestedBuffers;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (Ioctl(fd_, VIDIOC_REQBUFS, &request) < 0 || request.count < 2) {
        error = DeviceError("VIDIOC_REQBUFS", errno ? errno : ENOMEM);
        CloseDevice();
        return false;
    }

    buffers_.assign(request.count, {});
    for (uint32_t index = 0; index < request.count; ++index) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (Ioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
            error = DeviceError("VIDIOC_QUERYBUF", errno);
            CloseDevice();
            return false;
        }
        void* data = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_,
                          buffer.m.offset);
        if (data == MAP_FAILED) {
            error = DeviceError("mmap", errno);
            CloseDevice();
            return false;
        }
        buffers_[index] = {data, buffer.length};
    }
    for (uint32_t index = 0; index < request.count; ++index) {
        if (!Requeue(index)) {
            error = DeviceError("VIDIOC_QBUF", errno);
            CloseDevice();
            return false;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (Ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        error = DeviceError("VIDIOC_STREAMON", errno);
        CloseDevice();
        return false;
    }
    streaming_ = true;
    haveSequence_ = false;
    negotiated.bufferCount = static_cast<int>(request.count);
    v4l2_format active{};
    active.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (Ioctl(fd_, VIDIOC_G_FMT, &active) == 0 && active.fmt.pix.pixelformat == fourcc) {
        colorInput.colorspace = active.fmt.pix.colorspace;
        colorInput.ycbcrEncoding = active.fmt.pix.ycbcr_enc;
        colorInput.quantization = active.fmt.pix.quantization;
        colorInput.transferFunction = active.fmt.pix.xfer_func;
        negotiated.color = video::ResolveV4l2Color(colorInput, config_.colorOverride, config_.hdrInput);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        negotiated_ = negotiated;
    }
    diagnostics::Log("[capture] %s streaming %dx%d %s @ %s fps, %u buffers, %s %s %s (bytesperline %u)",
                     config_.devicePath.c_str(), negotiated.width, negotiated.height,
                     PixelFormatName(negotiated.format), FormatRate(negotiated.rate).c_str(),
                     request.count, video::MatrixName(negotiated.color.matrix),
                     video::RangeName(negotiated.color.range), video::TransferName(negotiated.color.transfer),
                     bytesPerLine_);
    return true;
}

void V4l2Capture::CloseDevice() {
    if (fd_ < 0) return;
    if (streaming_) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        Ioctl(fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }
    for (auto& buffer : buffers_) {
        if (buffer.data && buffer.data != MAP_FAILED) munmap(buffer.data, buffer.length);
    }
    buffers_.clear();
    v4l2_requestbuffers request{};
    request.count = 0;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    Ioctl(fd_, VIDIOC_REQBUFS, &request);
    close(fd_);
    fd_ = -1;
}

bool V4l2Capture::WaitForStop(int timeoutMs) {
    pollfd descriptor{wakeFd_, POLLIN, 0};
    const int ready = poll(&descriptor, 1, timeoutMs);
    return ready > 0 || stopping_.load();
}

void V4l2Capture::Run() {
    if (!SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL)) {
        diagnostics::Log("[capture] thread priority unchanged: %s", SDL_GetError());
    }
    while (!stopping_.load()) {
        if (fd_ < 0) {
            if (WaitForStop(1000)) break;
            std::string error;
            if (OpenDevice(error)) {
                state_ = CaptureState::Streaming;
                SetStatus({});
                diagnostics::Log("[capture] device reconnected");
            } else {
                SetStatus(error);
            }
            continue;
        }

        pollfd descriptors[2] = {{fd_, POLLIN, 0}, {wakeFd_, POLLIN, 0}};
        const int ready = poll(descriptors, 2, 1000);
        if (ready < 0) {
            if (errno == EINTR) continue;
            diagnostics::Log("[capture] poll failed: %s", std::strerror(errno));
            break;
        }
        if (descriptors[1].revents & POLLIN) break;
        if (ready == 0) continue;
        bool lost = (descriptors[0].revents & (POLLHUP | POLLNVAL)) != 0;
        if (!lost && (descriptors[0].revents & (POLLIN | POLLERR))) lost = !DrainAndPublish();
        if (lost) {
            diagnostics::Log("[capture] device lost; waiting for reconnection");
            CloseDevice();
            state_ = CaptureState::Reconnecting;
            SetStatus("capture device disconnected");
        }
    }
}

bool V4l2Capture::Requeue(uint32_t index) {
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    return Ioctl(fd_, VIDIOC_QBUF, &buffer) == 0;
}

void V4l2Capture::TrackSequence(uint32_t sequence) {
    if (haveSequence_ && sequence > lastSequence_ + 1) {
        counters_.driverDropped.fetch_add(sequence - lastSequence_ - 1, std::memory_order_relaxed);
    }
    lastSequence_ = sequence;
    haveSequence_ = true;
}

bool V4l2Capture::DrainAndPublish() {
    v4l2_buffer latest{};
    bool haveLatest = false;
    for (;;) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (Ioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
            if (errno == EAGAIN) break;
            diagnostics::Log("[capture] VIDIOC_DQBUF failed: %s", std::strerror(errno));
            return false;
        }
        TrackSequence(buffer.sequence);
        if (haveLatest) {
            counters_.skippedInQueue.fetch_add(1, std::memory_order_relaxed);
            if (!Requeue(latest.index)) return false;
        }
        latest = buffer;
        haveLatest = true;
    }
    if (!haveLatest) return true;

    const uint64_t dequeueNs = platform::MonotonicNs();
    bool usable = !(latest.flags & V4L2_BUF_FLAG_ERROR) && latest.bytesused > 0 &&
                  latest.index < buffers_.size();
    if (usable) {
        VideoFrame& frame = mailbox_->WriteSlot();
        usable = FillFrame(latest.index, latest.bytesused, frame);
        if (usable) {
            const uint64_t readyNs = platform::MonotonicNs();
            frame.dequeueNs = dequeueNs;
            frame.captureNs = TimestampNs(latest, dequeueNs);
            frame.readyNs = readyNs;
            frame.sequence = latest.sequence;
            frame.sourceFormat = config_.format;
            const video::ColorSpec color = frame.color;
            const uint64_t captureNs = frame.captureNs;
            if (mailbox_->Publish()) counters_.replaced.fetch_add(1, std::memory_order_relaxed);
            counters_.frames.fetch_add(1, std::memory_order_relaxed);
            counters_.lastFrameNs.store(readyNs, std::memory_order_release);
            counters_.processingNsTotal.fetch_add(readyNs - dequeueNs, std::memory_order_relaxed);
            counters_.processingSamples.fetch_add(1, std::memory_order_relaxed);
            if (onFrame_) onFrame_();
            if (RawFrameSink* sink = sink_.load(std::memory_order_acquire)) {
                RawFrame raw;
                raw.format = config_.format;
                raw.data = static_cast<const uint8_t*>(buffers_[latest.index].data);
                raw.size = latest.bytesused;
                raw.width = config_.width;
                raw.height = config_.height;
                raw.stride = static_cast<int>(bytesPerLine_);
                raw.color = color;
                raw.captureNs = captureNs;
                sink->OnRawFrame(raw);
            }
        }
    }
    if (!usable) counters_.corrupt.fetch_add(1, std::memory_order_relaxed);
    return Requeue(latest.index);
}

bool V4l2Capture::FillFrame(uint32_t index, uint32_t bytesUsed, VideoFrame& frame) {
    const auto* data = static_cast<const uint8_t*>(buffers_[index].data);
    const int width = config_.width;
    const int height = config_.height;
    const int stride = static_cast<int>(bytesPerLine_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frame.color = negotiated_.color;
    }
    frame.width = width;
    frame.height = height;

    switch (config_.format) {
    case PixelFormat::Nv12: {
        const size_t lumaBytes = static_cast<size_t>(stride) * static_cast<size_t>(height);
        const size_t chromaBytes = static_cast<size_t>(stride) * static_cast<size_t>(height / 2);
        if (bytesUsed < lumaBytes + chromaBytes) return false;
        frame.layout = FrameLayout::Nv12;
        frame.planeCount = 2;
        CopyPlane(frame.planes[0], data, stride, width, height, width);
        CopyPlane(frame.planes[1], data + lumaBytes, stride, width, height / 2, width / 2);
        return true;
    }
    case PixelFormat::P010: {
        const size_t lumaBytes = static_cast<size_t>(stride) * static_cast<size_t>(height);
        const size_t chromaBytes = static_cast<size_t>(stride) * static_cast<size_t>(height / 2);
        if (stride < width * 2 || bytesUsed < lumaBytes + chromaBytes) return false;
        frame.layout = FrameLayout::P010;
        frame.planeCount = 2;
        CopyPlane(frame.planes[0], data, stride, width * 2, height, width);
        CopyPlane(frame.planes[1], data + lumaBytes, stride, width * 2, height / 2, width / 2);
        return true;
    }
    case PixelFormat::Yuyv: {
        if (bytesUsed < static_cast<size_t>(stride) * static_cast<size_t>(height)) return false;
        frame.layout = FrameLayout::Yuyv;
        frame.planeCount = 1;
        CopyPlane(frame.planes[0], data, stride, width * 2, height, width / 2);
        return true;
    }
    case PixelFormat::Bgr24: {
        if (bytesUsed < static_cast<size_t>(stride) * static_cast<size_t>(height)) return false;
        frame.layout = FrameLayout::Bgr24;
        frame.planeCount = 1;
        CopyPlane(frame.planes[0], data, stride, width * 3, height, width);
        return true;
    }
    case PixelFormat::Mjpeg: {
        if (!decoder_.Decode(data, bytesUsed, frame)) return false;
        return frame.width > 0 && frame.height > 0;
    }
    }
    return false;
}

}
