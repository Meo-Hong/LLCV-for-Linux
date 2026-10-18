#pragma once

#include "recording/EncoderSupport.h"
#include "recording/RecordingOptions.h"
#include "video/VideoColor.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

#include <string>

namespace llcv::recording::ffmpeg {

struct VideoEncoderConfig {
    Codec codec = Codec::H264;
    Backend backend = Backend::Cpu;
    int width = 1280;
    int height = 720;
    AVRational frameRate{30, 1};
    RateControl rateControl = RateControl::Quality;
    Quality quality = Quality::Balanced;
    int videoMbps = 20;
    video::ColorSpec color;
};

struct VideoEncoder {
    AVCodecContext* context = nullptr;
    AVBufferRef* device = nullptr;
    AVBufferRef* frames = nullptr;
    AVPixelFormat inputFormat = AV_PIX_FMT_NONE;
    std::string name;

    VideoEncoder() = default;
    ~VideoEncoder();
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    bool Hardware() const { return frames != nullptr; }
    void Close();
};

void InitializeLogging();
const char* EncoderName(Codec codec, Backend backend);
bool OpenVideoEncoder(const VideoEncoderConfig& config, VideoEncoder& encoder, std::string& error);
std::string ErrorText(int code);
AVColorRange ColorRange(const video::ColorSpec& color);
AVColorSpace ColorMatrix(const video::ColorSpec& color);
AVColorPrimaries ColorPrimaries(const video::ColorSpec& color);
AVColorTransferCharacteristic ColorTransfer(const video::ColorSpec& color);

}
