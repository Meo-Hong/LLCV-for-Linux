#include "recording/Recorder.h"

#include "diagnostics/Logger.h"
#include "platform/Clock.h"
#include "platform/Paths.h"
#include "recording/FfmpegEncoder.h"
#include "recording/RecordingStorage.h"
#include "video/MjpegDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavutil/version.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
#define LLCV_FFMPEG_CHANNEL_LAYOUT 1
#else
#define LLCV_FFMPEG_CHANNEL_LAYOUT 0
#endif

namespace llcv::recording {
namespace {

constexpr int kSampleRate = 48000;
constexpr size_t kMinVideoPool = 8;
constexpr size_t kMaxVideoPool = 32;
constexpr double kVideoPoolSeconds = 0.75;
constexpr size_t kVideoPoolBytes = size_t{256} * 1024 * 1024;
constexpr int64_t kResyncFrames = kSampleRate * 40 / 1000;
constexpr uint64_t kOverloadNs = 3000000000ull;
constexpr size_t kMaxQueuedAudioFrames = kSampleRate * 3;
constexpr uint64_t kStorageCheckNs = 2000000000ull;
constexpr size_t kSilenceChunkFrames = kSampleRate;
constexpr AVRational kNanoseconds{1, 1000000000};

struct VideoBuffer {
    std::vector<uint8_t> data;
    size_t size = 0;
    int stride = 0;
    uint64_t captureNs = 0;
    uint64_t offsetNs = 0;
    video::ColorSpec color;
};

struct AudioChunk {
    std::vector<int16_t> samples;
    uint64_t startNs = 0;
    uint64_t offsetNs = 0;
    uint64_t resumeNs = 0;
};

struct FrameLimiter {
    int fps = 0;
    int inputFps = 0;
    int64_t first = -1;
    int64_t lastBucket = -1;

    bool Accept(int64_t timeNs) {
        if (fps <= 0) return true;
        if (first < 0) first = timeNs;
        if (timeNs < first) return false;
        const int64_t tolerance = inputFps > fps ? 500000000LL / std::max(1, inputFps) : 0;
        const int64_t bucket = ((timeNs - first + tolerance) * fps + fps - 1) / 1000000000LL;
        if (bucket <= lastBucket) return false;
        lastBucket = bucket;
        return true;
    }
};

struct SourceImage {
    AVPixelFormat format = AV_PIX_FMT_NONE;
    const uint8_t* planes[4] = {nullptr, nullptr, nullptr, nullptr};
    int strides[4] = {0, 0, 0, 0};
    int width = 0;
    int height = 0;
    bool rgb = false;
};

bool LimitsFps(const Options& options, double sourceFps) {
    return options.maxFps > 0 && sourceFps > options.maxFps + 0.5;
}

size_t RawFrameBytes(const VideoInput& input) {
    const size_t pixels = static_cast<size_t>(std::max(input.width, 1)) * static_cast<size_t>(std::max(input.height, 1));
    switch (input.format) {
    case capture::PixelFormat::Nv12: return pixels * 3 / 2;
    case capture::PixelFormat::Yuyv: return pixels * 2;
    case capture::PixelFormat::Bgr24: return pixels * 3;
    case capture::PixelFormat::Mjpeg: return pixels / 4;
    case capture::PixelFormat::P010: break;
    }
    return pixels * 3;
}

size_t VideoPoolSize(const StartRequest& request) {
    const double sourceFps = request.video.rate.Fps();
    const double fps = LimitsFps(request.options, sourceFps) ? request.options.maxFps : sourceFps;
    size_t count = std::clamp(static_cast<size_t>(std::ceil(fps * kVideoPoolSeconds)), kMinVideoPool, kMaxVideoPool);
    const size_t frameBytes = RawFrameBytes(request.video);
    while (count > kMinVideoPool && frameBytes * count > kVideoPoolBytes) --count;
    return count;
}

video::ColorSpec OutputColor(const VideoInput& input) {
    video::ColorSpec color = input.color;
    if (input.format == capture::PixelFormat::Bgr24) {
        color.matrix = input.height < 720 ? video::Matrix::Bt601 : video::Matrix::Bt709;
        color.range = video::Range::Limited;
    }
    color.transfer = video::Transfer::Sdr;
    return color;
}

int SwsSpace(video::Matrix matrix) {
    switch (matrix) {
    case video::Matrix::Bt601: return SWS_CS_ITU601;
    case video::Matrix::Bt2020: return SWS_CS_BT2020;
    case video::Matrix::Bt709: break;
    }
    return SWS_CS_ITU709;
}

AVPixelFormat PlanarYuvFormat(const capture::VideoFrame& frame) {
    const auto& luma = frame.planes[0];
    const auto& chroma = frame.planes[1];
    if (chroma.width <= 1 && chroma.height <= 1 && luma.width > 2) return AV_PIX_FMT_GRAY8;
    const bool horizontal = chroma.width < luma.width;
    const bool vertical = chroma.height < luma.height;
    if (horizontal && vertical) return AV_PIX_FMT_YUV420P;
    if (horizontal) return AV_PIX_FMT_YUV422P;
    if (vertical) return AV_PIX_FMT_YUV440P;
    return AV_PIX_FMT_YUV444P;
}

}

struct Recorder::Impl {
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable idle;
    std::thread worker;

    Phase phase = Phase::Idle;
    bool accepting = false;
    bool stopRequested = false;
    bool paused = false;
    bool overload = false;
    uint64_t startNs = 0;
    uint64_t pausedTotalNs = 0;
    uint64_t pauseBeganNs = 0;
    uint64_t resumeNs = 0;
    uint64_t endNs = 0;
    VideoInput input;
    bool wantAudio = false;
    FrameLimiter limiter;
    std::vector<std::unique_ptr<VideoBuffer>> freeVideo;
    std::deque<std::unique_ptr<VideoBuffer>> readyVideo;
    std::deque<AudioChunk> readyAudio;
    size_t queuedAudioFrames = 0;
    unsigned producers = 0;
    uint64_t droppedFrames = 0;
    uint64_t overloadSinceNs = 0;
    Status status;
    std::deque<Event> events;
    std::atomic<bool> active{false};

    class Session;

    void Run(StartRequest request, std::filesystem::path path);
    void Finish(Event event);
    void Recycle(std::deque<std::unique_ptr<VideoBuffer>>& buffers);
    uint64_t ElapsedLocked(uint64_t now) const;
};

class Recorder::Impl::Session {
public:
    Session(Impl& shared, const StartRequest& request, std::filesystem::path path)
        : shared_(shared), options_(request.options), input_(request.video), wantAudio_(request.audio),
          firstPath_(std::move(path)), currentPath_(firstPath_) {}

    ~Session() {
        if (format_) CloseSegment();
        sws_freeContext(sws_);
        av_frame_free(&swFrame_);
        av_frame_free(&hwFrame_);
        av_frame_free(&audioFrame_);
        av_packet_free(&packet_);
        avcodec_free_context(&audio_);
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool Open(std::string& error);
    void Loop();
    Event Finalize();
    void Abort();

    const std::string& EncoderName() const { return video_.name; }
    int Width() const { return outWidth_; }
    int Height() const { return outHeight_; }
    double Fps() const { return fps_; }

private:
    bool OpenAudio(std::string& error);
    bool OpenSegment(const std::filesystem::path& path, std::string& error);
    bool CloseSegment();
    bool Rotate(int64_t keyPts);
    void CheckSplit(int64_t pts);
    void CheckStorage();
    void PublishStatus();
    void Fail(Failure failure, const std::string& detail);

    void ProcessAudio(const AudioChunk& chunk);
    void WriteSilence(int64_t frames);
    void WriteSamples(const int16_t* samples, size_t frames);
    bool EncodePendingAudio(bool flush);
    bool EncodeAudioFrame(size_t offset, size_t available);

    void ProcessVideo(const VideoBuffer& buffer);
    bool BuildSource(const VideoBuffer& buffer, SourceImage& source);
    bool Scale(const SourceImage& source, const video::ColorSpec& color);
    bool SendVideo(int64_t pts);

    bool Drain(AVCodecContext* context, bool isVideo);
    bool WriteVideoPacket();
    bool WriteAudioPacket();
    bool Mux(AVRational from, AVStream* stream, int64_t base);

    Impl& shared_;
    Options options_;
    VideoInput input_;
    bool wantAudio_ = false;
    std::filesystem::path firstPath_;
    std::filesystem::path currentPath_;

    ffmpeg::VideoEncoder video_;
    AVCodecContext* audio_ = nullptr;
    AVFormatContext* format_ = nullptr;
    AVStream* videoStream_ = nullptr;
    AVStream* audioStream_ = nullptr;
    bool headerWritten_ = false;
    AVFrame* swFrame_ = nullptr;
    AVFrame* hwFrame_ = nullptr;
    AVFrame* audioFrame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ = nullptr;
    AVPixelFormat swsFormat_ = AV_PIX_FMT_NONE;
    int swsWidth_ = 0;
    int swsHeight_ = 0;
    video::ColorSpec swsColor_;
    video::MjpegDecoder decoder_;
    capture::VideoFrame decoded_;

    int outWidth_ = 0;
    int outHeight_ = 0;
    double fps_ = 0.0;
    video::ColorSpec outColor_;
    int64_t lastVideoPts_ = -1;
    bool forceKeyframe_ = false;
    bool splitPending_ = false;
    int segment_ = 1;
    int64_t segmentVideoBase_ = 0;
    int64_t segmentAudioBase_ = 0;
    uint64_t completedBytes_ = 0;
    bool anyPacket_ = false;
    std::optional<uint64_t> freeBytes_;

    int audioFrameSize_ = 1024;
    int64_t audioPts_ = 0;
    int64_t audioWritten_ = 0;
    bool audioStarted_ = false;
    std::vector<float> pendingLeft_;
    std::vector<float> pendingRight_;
    size_t pendingOffset_ = 0;
    float gain_ = 1.0f;

    Failure failure_ = Failure::None;
    std::string detail_;
};

bool Recorder::Impl::Session::Open(std::string& error) {
    ffmpeg::InitializeLogging();
    const VideoSize size = OutputSize(options_, input_.width, input_.height);
    outWidth_ = size.width;
    outHeight_ = size.height;
    AVRational frameRate{static_cast<int>(input_.rate.denominator), static_cast<int>(input_.rate.numerator)};
    if (LimitsFps(options_, input_.rate.Fps())) frameRate = AVRational{options_.maxFps, 1};
    if (frameRate.num <= 0 || frameRate.den <= 0) frameRate = AVRational{60, 1};
    fps_ = av_q2d(frameRate);
    outColor_ = OutputColor(input_);
    gain_ = static_cast<float>(std::clamp(options_.volumePercent, 0, 100)) / 100.0f;

    ffmpeg::VideoEncoderConfig config;
    config.codec = options_.codec;
    config.width = outWidth_;
    config.height = outHeight_;
    config.frameRate = frameRate;
    config.rateControl = options_.rateControl;
    config.quality = options_.quality;
    config.videoMbps = options_.videoMbps;
    config.color = outColor_;
    std::vector<Backend> backends;
    if (options_.encoder == EncoderChoice::Cpu) {
        backends = {Backend::Cpu};
    } else {
        backends = {Backend::Nvenc, Backend::Vaapi};
    }
    std::string attempts;
    for (const Backend backend : backends) {
        config.backend = backend;
        std::string attempt;
        if (ffmpeg::OpenVideoEncoder(config, video_, attempt)) break;
        if (!attempts.empty()) attempts += "; ";
        attempts += attempt;
    }
    if (!video_.context) {
        error = attempts;
        return false;
    }
    if (wantAudio_ && !OpenAudio(error)) return false;

    swFrame_ = av_frame_alloc();
    hwFrame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!swFrame_ || !hwFrame_ || !packet_) {
        error = "out of memory";
        return false;
    }
    swFrame_->format = video_.inputFormat;
    swFrame_->width = outWidth_;
    swFrame_->height = outHeight_;
    swFrame_->color_range = ffmpeg::ColorRange(outColor_);
    swFrame_->colorspace = ffmpeg::ColorMatrix(outColor_);
    swFrame_->color_primaries = ffmpeg::ColorPrimaries(outColor_);
    swFrame_->color_trc = ffmpeg::ColorTransfer(outColor_);
    const int result = av_frame_get_buffer(swFrame_, 0);
    if (result < 0) {
        error = "frame buffer: " + ffmpeg::ErrorText(result);
        return false;
    }
    if (!OpenSegment(currentPath_, error)) return false;
    freeBytes_ = FreeBytes(currentPath_.parent_path());
    diagnostics::Log("[recording] %s -> %s %dx%d @ %.3f fps, %s, audio %s, %s", capture::PixelFormatName(input_.format),
                     video_.name.c_str(), outWidth_, outHeight_, fps_,
                     options_.rateControl == RateControl::Quality ? "CQP" : "bitrate",
                     audio_ ? "AAC" : "none", currentPath_.c_str());
    return true;
}

bool Recorder::Impl::Session::OpenAudio(std::string& error) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        error = "AAC encoder is not available in this FFmpeg build";
        return false;
    }
    audio_ = avcodec_alloc_context3(codec);
    if (!audio_) {
        error = "AAC: out of memory";
        return false;
    }
    audio_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audio_->sample_rate = kSampleRate;
    audio_->bit_rate = static_cast<int64_t>(options_.audioKbps) * 1000;
    audio_->time_base = AVRational{1, kSampleRate};
    audio_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
#if LLCV_FFMPEG_CHANNEL_LAYOUT
    av_channel_layout_default(&audio_->ch_layout, 2);
#else
    audio_->channel_layout = AV_CH_LAYOUT_STEREO;
    audio_->channels = 2;
#endif
    const int result = avcodec_open2(audio_, codec, nullptr);
    if (result < 0) {
        error = "AAC: " + ffmpeg::ErrorText(result);
        return false;
    }
    audioFrameSize_ = audio_->frame_size > 0 ? audio_->frame_size : 1024;
    audioFrame_ = av_frame_alloc();
    if (!audioFrame_) {
        error = "AAC: out of memory";
        return false;
    }
    return true;
}

bool Recorder::Impl::Session::OpenSegment(const std::filesystem::path& path, std::string& error) {
    int result = avformat_alloc_output_context2(&format_, nullptr, "mp4", path.c_str());
    if (result < 0 || !format_) {
        error = "MP4 muxer: " + ffmpeg::ErrorText(result);
        format_ = nullptr;
        return false;
    }
    videoStream_ = avformat_new_stream(format_, nullptr);
    if (!videoStream_ || avcodec_parameters_from_context(videoStream_->codecpar, video_.context) < 0) {
        error = "MP4 video track could not be created";
        return false;
    }
    videoStream_->time_base = video_.context->time_base;
    videoStream_->avg_frame_rate = video_.context->framerate;
    videoStream_->codecpar->format = video_.inputFormat;
    if (options_.codec == Codec::Hevc) videoStream_->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
    if (audio_) {
        audioStream_ = avformat_new_stream(format_, nullptr);
        if (!audioStream_ || avcodec_parameters_from_context(audioStream_->codecpar, audio_) < 0) {
            error = "MP4 audio track could not be created";
            return false;
        }
        audioStream_->time_base = audio_->time_base;
    }
    result = avio_open(&format_->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (result < 0) {
        error = path.string() + ": " + ffmpeg::ErrorText(result);
        return false;
    }
    AVDictionary* muxOptions = nullptr;
    av_dict_set(&muxOptions, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    result = avformat_write_header(format_, &muxOptions);
    av_dict_free(&muxOptions);
    if (result < 0) {
        error = "MP4 header: " + ffmpeg::ErrorText(result);
        return false;
    }
    headerWritten_ = true;
    return true;
}

bool Recorder::Impl::Session::CloseSegment() {
    if (!format_) return true;
    bool ok = true;
    if (headerWritten_) {
        const int result = av_write_trailer(format_);
        if (result < 0) {
            ok = false;
            if (detail_.empty()) detail_ = "MP4 trailer: " + ffmpeg::ErrorText(result);
        }
    }
    if (format_->pb) avio_closep(&format_->pb);
    avformat_free_context(format_);
    format_ = nullptr;
    videoStream_ = nullptr;
    audioStream_ = nullptr;
    headerWritten_ = false;
    std::error_code error;
    const auto size = std::filesystem::file_size(currentPath_, error);
    if (!error) completedBytes_ += size;
    return ok;
}

bool Recorder::Impl::Session::Rotate(int64_t keyPts) {
    if (!CloseSegment()) {
        Fail(Failure::WriteFailed, detail_);
        return false;
    }
    ++segment_;
    currentPath_ = SegmentPath(firstPath_, segment_);
    segmentVideoBase_ = keyPts;
    segmentAudioBase_ = audio_ ? av_rescale_q(keyPts, video_.context->time_base, audio_->time_base) : 0;
    splitPending_ = false;
    std::string error;
    if (!OpenSegment(currentPath_, error)) {
        Fail(Failure::WriteFailed, error);
        return false;
    }
    diagnostics::Log("[recording] segment %d: %s", segment_, currentPath_.c_str());
    return true;
}

void Recorder::Impl::Session::CheckSplit(int64_t pts) {
    if (splitPending_ || !format_) return;
    bool due = false;
    switch (options_.splitMode) {
    case SplitMode::Off: break;
    case SplitMode::Duration: {
        const int64_t limit = av_rescale_q(static_cast<int64_t>(options_.splitMinutes) * 60, AVRational{1, 1},
                                           video_.context->time_base);
        due = pts - segmentVideoBase_ >= limit;
        break;
    }
    case SplitMode::Size:
        due = format_->pb && avio_tell(format_->pb) >= (static_cast<int64_t>(options_.splitGib) << 30);
        break;
    }
    if (due) {
        splitPending_ = true;
        forceKeyframe_ = true;
    }
}

void Recorder::Impl::Session::CheckStorage() {
    freeBytes_ = FreeBytes(currentPath_.parent_path());
    if (freeBytes_ && *freeBytes_ < ReserveBytes(options_)) {
        Fail(Failure::DiskFull, "free space below the recording reserve");
    }
}

void Recorder::Impl::Session::PublishStatus() {
    const uint64_t current = format_ && format_->pb ? static_cast<uint64_t>(std::max<int64_t>(0, avio_tell(format_->pb))) : 0;
    std::lock_guard<std::mutex> lock(shared_.mutex);
    shared_.status.bytes = completedBytes_ + current;
    shared_.status.segment = segment_;
    shared_.status.path = currentPath_;
    shared_.status.freeBytes = freeBytes_;
}

void Recorder::Impl::Session::Fail(Failure failure, const std::string& detail) {
    if (failure_ != Failure::None) return;
    failure_ = failure;
    detail_ = detail;
    diagnostics::Log("[recording] stopping: %s", detail.c_str());
}

void Recorder::Impl::Session::Loop() {
    uint64_t nextStorageCheck = platform::MonotonicNs() + kStorageCheckNs;
    std::deque<AudioChunk> audioBatch;
    std::deque<std::unique_ptr<VideoBuffer>> videoBatch;
    for (;;) {
        bool stopping = false;
        bool overloaded = false;
        {
            std::unique_lock<std::mutex> lock(shared_.mutex);
            shared_.wake.wait_for(lock, std::chrono::milliseconds(250), [this] {
                return (shared_.stopRequested && shared_.producers == 0) || shared_.overload ||
                       !shared_.readyVideo.empty() || !shared_.readyAudio.empty();
            });
            audioBatch.swap(shared_.readyAudio);
            shared_.queuedAudioFrames = 0;
            videoBatch.swap(shared_.readyVideo);
            stopping = shared_.stopRequested && shared_.producers == 0;
            overloaded = shared_.overload;
        }
        if (overloaded) Fail(Failure::Overload, "encoding or writing could not keep up");
        for (const auto& chunk : audioBatch) {
            if (failure_ != Failure::None) break;
            ProcessAudio(chunk);
        }
        audioBatch.clear();
        for (const auto& buffer : videoBatch) {
            if (failure_ != Failure::None) break;
            ProcessVideo(*buffer);
        }
        shared_.Recycle(videoBatch);
        const uint64_t now = platform::MonotonicNs();
        if (failure_ == Failure::None && now >= nextStorageCheck) {
            CheckStorage();
            nextStorageCheck = now + kStorageCheckNs;
        }
        PublishStatus();
        if (failure_ != Failure::None || stopping) return;
    }
}

Event Recorder::Impl::Session::Finalize() {
    if (failure_ != Failure::WriteFailed && format_) {
        if (audio_ && EncodePendingAudio(true)) {
            if (avcodec_send_frame(audio_, nullptr) >= 0) Drain(audio_, false);
        }
        if (video_.context && avcodec_send_frame(video_.context, nullptr) >= 0) Drain(video_.context, true);
    }
    const bool closed = CloseSegment();
    PublishStatus();
    Event event;
    event.path = currentPath_;
    event.segments = segment_;
    event.fileKept = anyPacket_;
    if (!anyPacket_) {
        std::error_code error;
        std::filesystem::remove(currentPath_, error);
        event.kind = failure_ == Failure::None ? Event::Kind::Empty : Event::Kind::Failed;
        event.failure = failure_;
        event.detail = detail_;
        return event;
    }
    if (failure_ == Failure::None && !closed) Fail(Failure::FinalizeFailed, detail_);
    event.kind = failure_ == Failure::None ? Event::Kind::Saved : Event::Kind::Failed;
    event.failure = failure_;
    event.detail = detail_;
    diagnostics::Log("[recording] %s %s (%d segment%s, %llu bytes)", event.kind == Event::Kind::Saved ? "saved" : "kept",
                     currentPath_.c_str(), segment_, segment_ == 1 ? "" : "s",
                     static_cast<unsigned long long>(completedBytes_));
    return event;
}

void Recorder::Impl::Session::Abort() {
    if (format_) CloseSegment();
    std::error_code error;
    if (!anyPacket_) std::filesystem::remove(currentPath_, error);
}

void Recorder::Impl::Session::ProcessAudio(const AudioChunk& chunk) {
    if (!audio_) return;
    const size_t frames = chunk.samples.size() / 2;
    size_t skip = 0;
    uint64_t start = chunk.startNs;
    if (start < chunk.resumeNs) {
        skip = static_cast<size_t>((chunk.resumeNs - start) * kSampleRate / 1000000000ull);
        if (skip >= frames) return;
        start += static_cast<uint64_t>(skip) * 1000000000ull / kSampleRate;
    }
    const int64_t expected =
        start > chunk.offsetNs ? static_cast<int64_t>((start - chunk.offsetNs) * kSampleRate / 1000000000ull) : 0;
    const int64_t drift = expected - audioWritten_;
    if (!audioStarted_ || drift > kResyncFrames) {
        if (drift > 0) WriteSilence(drift);
        audioStarted_ = true;
    } else if (drift < -kResyncFrames) {
        skip += std::min(frames - skip, static_cast<size_t>(-drift));
        if (skip >= frames) return;
    }
    WriteSamples(chunk.samples.data() + skip * 2, frames - skip);
}

void Recorder::Impl::Session::WriteSilence(int64_t frames) {
    while (frames > 0 && failure_ == Failure::None) {
        const size_t chunk = static_cast<size_t>(std::min<int64_t>(frames, static_cast<int64_t>(kSilenceChunkFrames)));
        pendingLeft_.insert(pendingLeft_.end(), chunk, 0.0f);
        pendingRight_.insert(pendingRight_.end(), chunk, 0.0f);
        audioWritten_ += static_cast<int64_t>(chunk);
        frames -= static_cast<int64_t>(chunk);
        if (!EncodePendingAudio(false)) return;
    }
}

void Recorder::Impl::Session::WriteSamples(const int16_t* samples, size_t frames) {
    const float scale = gain_ / 32768.0f;
    for (size_t i = 0; i < frames; ++i) {
        pendingLeft_.push_back(static_cast<float>(samples[i * 2]) * scale);
        pendingRight_.push_back(static_cast<float>(samples[i * 2 + 1]) * scale);
    }
    audioWritten_ += static_cast<int64_t>(frames);
    EncodePendingAudio(false);
}

bool Recorder::Impl::Session::EncodePendingAudio(bool flush) {
    const size_t frameSize = static_cast<size_t>(audioFrameSize_);
    while (pendingLeft_.size() - pendingOffset_ >= frameSize) {
        if (!EncodeAudioFrame(pendingOffset_, frameSize)) return false;
        pendingOffset_ += frameSize;
    }
    if (flush && pendingLeft_.size() > pendingOffset_) {
        if (!EncodeAudioFrame(pendingOffset_, pendingLeft_.size() - pendingOffset_)) return false;
        pendingOffset_ = pendingLeft_.size();
    }
    if (pendingOffset_ > 0 && (pendingOffset_ >= frameSize * 8 || pendingOffset_ == pendingLeft_.size())) {
        const auto offset = static_cast<std::ptrdiff_t>(pendingOffset_);
        pendingLeft_.erase(pendingLeft_.begin(), pendingLeft_.begin() + offset);
        pendingRight_.erase(pendingRight_.begin(), pendingRight_.begin() + offset);
        pendingOffset_ = 0;
    }
    return true;
}

bool Recorder::Impl::Session::EncodeAudioFrame(size_t offset, size_t available) {
    av_frame_unref(audioFrame_);
    audioFrame_->nb_samples = audioFrameSize_;
    audioFrame_->format = AV_SAMPLE_FMT_FLTP;
    audioFrame_->sample_rate = kSampleRate;
#if LLCV_FFMPEG_CHANNEL_LAYOUT
    if (av_channel_layout_copy(&audioFrame_->ch_layout, &audio_->ch_layout) < 0) {
        Fail(Failure::EncoderFailed, "AAC: channel layout");
        return false;
    }
#else
    audioFrame_->channel_layout = audio_->channel_layout;
    audioFrame_->channels = 2;
#endif
    int result = av_frame_get_buffer(audioFrame_, 0);
    if (result < 0) {
        Fail(Failure::EncoderFailed, "AAC frame: " + ffmpeg::ErrorText(result));
        return false;
    }
    const size_t count = std::min(available, static_cast<size_t>(audioFrameSize_));
    auto* left = reinterpret_cast<float*>(audioFrame_->data[0]);
    auto* right = reinterpret_cast<float*>(audioFrame_->data[1]);
    std::memcpy(left, pendingLeft_.data() + offset, count * sizeof(float));
    std::memcpy(right, pendingRight_.data() + offset, count * sizeof(float));
    std::fill(left + count, left + audioFrameSize_, 0.0f);
    std::fill(right + count, right + audioFrameSize_, 0.0f);
    audioFrame_->pts = audioPts_;
    audioPts_ += audioFrameSize_;
    result = avcodec_send_frame(audio_, audioFrame_);
    if (result < 0) {
        Fail(Failure::EncoderFailed, "AAC: " + ffmpeg::ErrorText(result));
        return false;
    }
    return Drain(audio_, false);
}

void Recorder::Impl::Session::ProcessVideo(const VideoBuffer& buffer) {
    SourceImage source;
    if (!BuildSource(buffer, source)) return;
    if (!Scale(source, buffer.color)) return;
    const int64_t ptsNs = buffer.captureNs > buffer.offsetNs ? static_cast<int64_t>(buffer.captureNs - buffer.offsetNs) : 0;
    int64_t pts = av_rescale_q(ptsNs, kNanoseconds, video_.context->time_base);
    if (pts <= lastVideoPts_) pts = lastVideoPts_ + 1;
    lastVideoPts_ = pts;
    SendVideo(pts);
}

bool Recorder::Impl::Session::BuildSource(const VideoBuffer& buffer, SourceImage& source) {
    const int width = input_.width;
    const int height = input_.height;
    const int stride = buffer.stride;
    const uint8_t* data = buffer.data.data();
    source.width = width;
    source.height = height;
    switch (input_.format) {
    case capture::PixelFormat::Nv12: {
        const size_t luma = static_cast<size_t>(stride) * static_cast<size_t>(height);
        if (stride < width || buffer.size < luma + luma / 2) return false;
        source.format = AV_PIX_FMT_NV12;
        source.planes[0] = data;
        source.planes[1] = data + luma;
        source.strides[0] = stride;
        source.strides[1] = stride;
        return true;
    }
    case capture::PixelFormat::Yuyv:
        if (stride < width * 2 || buffer.size < static_cast<size_t>(stride) * static_cast<size_t>(height)) return false;
        source.format = AV_PIX_FMT_YUYV422;
        source.planes[0] = data;
        source.strides[0] = stride;
        return true;
    case capture::PixelFormat::Bgr24:
        if (stride < width * 3 || buffer.size < static_cast<size_t>(stride) * static_cast<size_t>(height)) return false;
        source.format = AV_PIX_FMT_BGR24;
        source.planes[0] = data;
        source.strides[0] = stride;
        source.rgb = true;
        return true;
    case capture::PixelFormat::Mjpeg:
        if (!decoder_.Decode(data, buffer.size, decoded_) || decoded_.width <= 0 || decoded_.height <= 0) return false;
        source.width = decoded_.width;
        source.height = decoded_.height;
        if (decoded_.layout == capture::FrameLayout::Rgb24) {
            source.format = AV_PIX_FMT_RGB24;
            source.planes[0] = decoded_.planes[0].data.data();
            source.strides[0] = decoded_.planes[0].stride;
            source.rgb = true;
            return true;
        }
        if (decoded_.layout != capture::FrameLayout::Yuv3Plane) return false;
        source.format = PlanarYuvFormat(decoded_);
        for (int plane = 0; plane < (source.format == AV_PIX_FMT_GRAY8 ? 1 : 3); ++plane) {
            source.planes[plane] = decoded_.planes[static_cast<size_t>(plane)].data.data();
            source.strides[plane] = decoded_.planes[static_cast<size_t>(plane)].stride;
        }
        return true;
    case capture::PixelFormat::P010:
        break;
    }
    return false;
}

bool Recorder::Impl::Session::Scale(const SourceImage& source, const video::ColorSpec& color) {
    const bool colorChanged = color.matrix != swsColor_.matrix || color.range != swsColor_.range;
    if (!sws_ || swsFormat_ != source.format || swsWidth_ != source.width || swsHeight_ != source.height ||
        (!source.rgb && colorChanged)) {
        sws_freeContext(sws_);
        sws_ = sws_getContext(source.width, source.height, source.format, outWidth_, outHeight_, video_.inputFormat,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) {
            Fail(Failure::EncoderFailed, "the video scaler could not be created");
            return false;
        }
        const int* sourceTable = sws_getCoefficients(source.rgb ? SWS_CS_DEFAULT : SwsSpace(color.matrix));
        const int* targetTable = sws_getCoefficients(SwsSpace(outColor_.matrix));
        const int sourceRange = source.rgb || color.range == video::Range::Full ? 1 : 0;
        const int targetRange = outColor_.range == video::Range::Full ? 1 : 0;
        sws_setColorspaceDetails(sws_, sourceTable, sourceRange, targetTable, targetRange, 0, 1 << 16, 1 << 16);
        swsFormat_ = source.format;
        swsWidth_ = source.width;
        swsHeight_ = source.height;
        swsColor_ = color;
    }
    const int result = av_frame_make_writable(swFrame_);
    if (result < 0) {
        Fail(Failure::EncoderFailed, "frame buffer: " + ffmpeg::ErrorText(result));
        return false;
    }
    sws_scale(sws_, source.planes, source.strides, 0, source.height, swFrame_->data, swFrame_->linesize);
    return true;
}

bool Recorder::Impl::Session::SendVideo(int64_t pts) {
    AVFrame* frame = swFrame_;
    if (video_.Hardware()) {
        av_frame_unref(hwFrame_);
        int result = av_hwframe_get_buffer(video_.frames, hwFrame_, 0);
        if (result >= 0) result = av_hwframe_transfer_data(hwFrame_, swFrame_, 0);
        if (result >= 0) result = av_frame_copy_props(hwFrame_, swFrame_);
        if (result < 0) {
            Fail(Failure::EncoderFailed, video_.name + " upload: " + ffmpeg::ErrorText(result));
            return false;
        }
        frame = hwFrame_;
    }
    frame->pts = pts;
    frame->pict_type = forceKeyframe_ ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    forceKeyframe_ = false;
    const int result = avcodec_send_frame(video_.context, frame);
    if (result < 0) {
        Fail(Failure::EncoderFailed, video_.name + ": " + ffmpeg::ErrorText(result));
        return false;
    }
    return Drain(video_.context, true);
}

bool Recorder::Impl::Session::Drain(AVCodecContext* context, bool isVideo) {
    for (;;) {
        const int result = avcodec_receive_packet(context, packet_);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
        if (result < 0) {
            Fail(Failure::EncoderFailed, (isVideo ? video_.name : std::string("AAC")) + ": " + ffmpeg::ErrorText(result));
            return false;
        }
        const bool written = isVideo ? WriteVideoPacket() : WriteAudioPacket();
        av_packet_unref(packet_);
        if (!written) return false;
    }
}

bool Recorder::Impl::Session::WriteVideoPacket() {
    if (!format_) return false;
    const int64_t pts = packet_->pts;
    if (splitPending_ && (packet_->flags & AV_PKT_FLAG_KEY) && pts != AV_NOPTS_VALUE) {
        if (!Rotate(pts)) return false;
    }
    if (!Mux(video_.context->time_base, videoStream_, segmentVideoBase_)) return false;
    if (pts != AV_NOPTS_VALUE) CheckSplit(pts);
    return true;
}

bool Recorder::Impl::Session::WriteAudioPacket() {
    if (!format_ || !audioStream_) return format_ != nullptr;
    if (packet_->pts != AV_NOPTS_VALUE && packet_->pts < segmentAudioBase_) return true;
    return Mux(audio_->time_base, audioStream_, segmentAudioBase_);
}

bool Recorder::Impl::Session::Mux(AVRational from, AVStream* stream, int64_t base) {
    if (packet_->pts != AV_NOPTS_VALUE) packet_->pts -= base;
    if (packet_->dts != AV_NOPTS_VALUE) packet_->dts -= base;
    packet_->stream_index = stream->index;
    av_packet_rescale_ts(packet_, from, stream->time_base);
    const int result = av_write_frame(format_, packet_);
    if (result < 0) {
        Fail(Failure::WriteFailed, currentPath_.string() + ": " + ffmpeg::ErrorText(result));
        return false;
    }
    anyPacket_ = true;
    return true;
}

void Recorder::Impl::Run(StartRequest request, std::filesystem::path path) {
    Session session(*this, request, path);
    std::string error;
    if (!session.Open(error)) {
        diagnostics::Log("[recording] start failed: %s", error.c_str());
        session.Abort();
        Event event;
        event.kind = Event::Kind::Failed;
        event.failure = Failure::EncoderFailed;
        event.path = path;
        event.detail = error;
        Finish(std::move(event));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        startNs = platform::MonotonicNs();
        resumeNs = startNs;
        pausedTotalNs = 0;
        pauseBeganNs = 0;
        status.encoder = session.EncoderName();
        status.width = session.Width();
        status.height = session.Height();
        status.fps = session.Fps();
        status.path = path;
        status.segment = 1;
        if (stopRequested) {
            endNs = startNs;
        } else {
            accepting = true;
        }
        phase = Phase::Recording;
    }
    session.Loop();
    {
        std::lock_guard<std::mutex> lock(mutex);
        accepting = false;
        if (endNs == 0) endNs = platform::MonotonicNs();
        phase = Phase::Finalizing;
    }
    Finish(session.Finalize());
}

void Recorder::Impl::Recycle(std::deque<std::unique_ptr<VideoBuffer>>& buffers) {
    if (buffers.empty()) return;
    std::lock_guard<std::mutex> lock(mutex);
    while (!buffers.empty()) {
        freeVideo.push_back(std::move(buffers.front()));
        buffers.pop_front();
    }
}

void Recorder::Impl::Finish(Event event) {
    std::lock_guard<std::mutex> lock(mutex);
    while (!readyVideo.empty()) {
        freeVideo.push_back(std::move(readyVideo.front()));
        readyVideo.pop_front();
    }
    readyAudio.clear();
    queuedAudioFrames = 0;
    accepting = false;
    stopRequested = false;
    paused = false;
    overload = false;
    phase = Phase::Idle;
    events.push_back(std::move(event));
    active.store(false, std::memory_order_release);
    idle.notify_all();
}

uint64_t Recorder::Impl::ElapsedLocked(uint64_t now) const {
    if (startNs == 0 || phase == Phase::Idle || phase == Phase::Starting) return 0;
    const uint64_t end = endNs ? endNs : now;
    uint64_t excluded = pausedTotalNs;
    if (paused && end > pauseBeganNs) excluded += end - pauseBeganNs;
    const uint64_t span = end > startNs ? end - startNs : 0;
    return span > excluded ? (span - excluded) / 1000000ull : 0;
}

Recorder::Recorder() : impl_(std::make_shared<Impl>()) {}

Recorder::~Recorder() {
    Stop();
    if (WaitIdle(10000)) return;
    if (impl_->worker.joinable()) impl_->worker.detach();
}

StartError Recorder::Start(const StartRequest& request) {
    Impl& d = *impl_;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        if (d.phase != Phase::Idle) return StartError::Busy;
    }
    if (d.worker.joinable()) d.worker.join();
    switch (request.video.format) {
    case capture::PixelFormat::Nv12:
    case capture::PixelFormat::Yuyv:
    case capture::PixelFormat::Mjpeg:
    case capture::PixelFormat::Bgr24:
        break;
    case capture::PixelFormat::P010:
        return StartError::HdrInput;
    }
    if (request.video.color.transfer == video::Transfer::Pq) return StartError::HdrInput;
    if (request.video.width <= 0 || request.video.height <= 0) return StartError::UnsupportedFormat;
    if (!platform::EnsureDirectory(request.folder)) return StartError::StorageUnavailable;
    const auto available = FreeBytes(request.folder);
    if (available && *available < ReserveBytes(request.options)) return StartError::DiskFull;
    const std::filesystem::path path = NewRecordingPath(request.folder);

    {
        std::lock_guard<std::mutex> lock(d.mutex);
        while (!d.readyVideo.empty()) {
            d.freeVideo.push_back(std::move(d.readyVideo.front()));
            d.readyVideo.pop_front();
        }
        const size_t poolSize = VideoPoolSize(request);
        while (d.freeVideo.size() > poolSize) d.freeVideo.pop_back();
        while (d.freeVideo.size() < poolSize) d.freeVideo.push_back(std::make_unique<VideoBuffer>());
        d.readyAudio.clear();
        d.queuedAudioFrames = 0;
        d.phase = Phase::Starting;
        d.accepting = false;
        d.stopRequested = false;
        d.paused = false;
        d.overload = false;
        d.startNs = 0;
        d.pausedTotalNs = 0;
        d.pauseBeganNs = 0;
        d.resumeNs = 0;
        d.endNs = 0;
        d.input = request.video;
        d.wantAudio = request.audio;
        d.limiter = FrameLimiter{};
        const double sourceFps = request.video.rate.Fps();
        if (LimitsFps(request.options, sourceFps)) {
            d.limiter.fps = request.options.maxFps;
            d.limiter.inputFps = static_cast<int>(std::lround(sourceFps));
        }
        d.droppedFrames = 0;
        d.overloadSinceNs = 0;
        d.status = Status{};
        d.status.path = path;
        d.status.freeBytes = available;
        d.active.store(true, std::memory_order_release);
    }
    std::shared_ptr<Impl> keep = impl_;
    d.worker = std::thread([keep, request, path] { keep->Run(request, path); });
    return StartError::None;
}

void Recorder::Stop() {
    Impl& d = *impl_;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        if (d.phase != Phase::Starting && d.phase != Phase::Recording) return;
        d.stopRequested = true;
        d.accepting = false;
        if (d.phase == Phase::Recording && d.endNs == 0) d.endNs = platform::MonotonicNs();
    }
    d.wake.notify_all();
}

bool Recorder::WaitIdle(int timeoutMs) {
    Impl& d = *impl_;
    bool idle = false;
    {
        std::unique_lock<std::mutex> lock(d.mutex);
        idle = d.idle.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&d] { return d.phase == Phase::Idle; });
    }
    if (idle && d.worker.joinable()) d.worker.join();
    return idle;
}

void Recorder::SetPaused(bool paused) {
    Impl& d = *impl_;
    std::lock_guard<std::mutex> lock(d.mutex);
    if (d.phase != Phase::Recording || !d.accepting) return;
    const uint64_t now = platform::MonotonicNs();
    if (paused && !d.paused) {
        d.paused = true;
        d.pauseBeganNs = now;
        diagnostics::Log("[recording] paused");
    } else if (!paused && d.paused) {
        d.paused = false;
        d.pausedTotalNs += now - d.pauseBeganNs;
        d.resumeNs = now;
        diagnostics::Log("[recording] resumed");
    }
}

Status Recorder::GetStatus() const {
    const Impl& d = *impl_;
    std::lock_guard<std::mutex> lock(d.mutex);
    Status status = d.status;
    status.phase = d.phase;
    status.paused = d.paused;
    status.droppedFrames = d.droppedFrames;
    status.elapsedMs = d.ElapsedLocked(platform::MonotonicNs());
    return status;
}

std::optional<Event> Recorder::PollEvent() {
    Impl& d = *impl_;
    std::lock_guard<std::mutex> lock(d.mutex);
    if (d.events.empty()) return std::nullopt;
    Event event = std::move(d.events.front());
    d.events.pop_front();
    return event;
}

bool Recorder::Busy() const {
    const Impl& d = *impl_;
    std::lock_guard<std::mutex> lock(d.mutex);
    return d.phase != Phase::Idle;
}

void Recorder::OnRawFrame(const capture::RawFrame& frame) {
    Impl& d = *impl_;
    if (!d.active.load(std::memory_order_acquire) || !frame.data || frame.size == 0) return;
    std::unique_ptr<VideoBuffer> buffer;
    uint64_t offset = 0;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        if (!d.accepting || d.paused) return;
        if (frame.format != d.input.format || frame.width != d.input.width || frame.height != d.input.height) return;
        if (frame.captureNs < d.resumeNs) return;
        if (!d.limiter.Accept(static_cast<int64_t>(frame.captureNs))) return;
        if (d.freeVideo.empty()) {
            ++d.droppedFrames;
            const uint64_t now = platform::MonotonicNs();
            if (d.overloadSinceNs == 0) {
                d.overloadSinceNs = now;
            } else if (now - d.overloadSinceNs >= kOverloadNs && !d.overload) {
                d.overload = true;
                d.wake.notify_one();
            }
            return;
        }
        d.overloadSinceNs = 0;
        buffer = std::move(d.freeVideo.back());
        d.freeVideo.pop_back();
        offset = d.startNs + d.pausedTotalNs;
        ++d.producers;
    }
    if (buffer->data.size() < frame.size) buffer->data.resize(frame.size);
    std::memcpy(buffer->data.data(), frame.data, frame.size);
    buffer->size = frame.size;
    buffer->stride = frame.stride;
    buffer->captureNs = frame.captureNs;
    buffer->offsetNs = offset;
    buffer->color = frame.color;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        d.readyVideo.push_back(std::move(buffer));
        --d.producers;
    }
    d.wake.notify_one();
}

void Recorder::OnCapturedPcm(const int16_t* samples, size_t frames, uint64_t endNs) {
    Impl& d = *impl_;
    if (!d.active.load(std::memory_order_acquire) || !samples || frames == 0) return;
    AudioChunk chunk;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        if (!d.accepting || d.paused || !d.wantAudio) return;
        if (endNs <= d.resumeNs) return;
        if (d.queuedAudioFrames + frames > kMaxQueuedAudioFrames) {
            if (!d.overload) {
                d.overload = true;
                d.wake.notify_one();
            }
            return;
        }
        d.queuedAudioFrames += frames;
        chunk.offsetNs = d.startNs + d.pausedTotalNs;
        chunk.resumeNs = d.resumeNs;
        ++d.producers;
    }
    const uint64_t durationNs = static_cast<uint64_t>(frames) * 1000000000ull / kSampleRate;
    chunk.samples.assign(samples, samples + frames * 2);
    chunk.startNs = endNs > durationNs ? endNs - durationNs : 0;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        d.readyAudio.push_back(std::move(chunk));
        --d.producers;
    }
    d.wake.notify_one();
}

}
