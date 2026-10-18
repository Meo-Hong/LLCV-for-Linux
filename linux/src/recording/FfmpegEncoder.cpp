#include "recording/FfmpegEncoder.h"

#include "diagnostics/Logger.h"
#include "platform/Strings.h"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace llcv::recording::ffmpeg {
namespace {

std::once_flag g_logging;

void ForwardLog(void* object, int level, const char* format, va_list arguments) {
    if (level > AV_LOG_ERROR) return;
    char line[1024];
    int prefix = 1;
    av_log_format_line(object, level, format, arguments, line, sizeof(line), &prefix);
    size_t length = std::strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = '\0';
    if (length > 0) diagnostics::Log("[ffmpeg] %s", line);
}

bool SetString(AVCodecContext* context, const char* name, const char* value) {
    return av_opt_set(context->priv_data, name, value, 0) >= 0;
}

bool SetInt(AVCodecContext* context, const char* name, int64_t value) {
    return av_opt_set_int(context->priv_data, name, value, 0) >= 0;
}

int QuantizerFor(Codec codec, Backend backend, Quality quality) {
    const int qp = QualityQp(quality);
    if (codec != Codec::Av1) return qp;
    const int qindex = qp * 5 + 5;
    return backend == Backend::Cpu ? qindex / 4 : qindex;
}

void ApplyBitrate(AVCodecContext* context, int mbps) {
    const int64_t bitrate = static_cast<int64_t>(mbps) * 1000000;
    context->bit_rate = bitrate;
    context->rc_max_rate = bitrate * 3 / 2;
    context->rc_buffer_size = static_cast<int>(bitrate * 2);
}

bool ConfigureCpu(const VideoEncoderConfig& config, AVCodecContext* context, const std::string& name,
                  std::string& error) {
    const bool quality = config.rateControl == RateControl::Quality;
    const int quantizer = QuantizerFor(config.codec, Backend::Cpu, config.quality);
    switch (config.codec) {
    case Codec::H264:
        context->pix_fmt = AV_PIX_FMT_NV12;
        SetString(context, "preset", "veryfast");
        SetInt(context, "forced-idr", 1);
        if (quality && !SetInt(context, "qp", quantizer)) {
            error = name + " has no constant-QP mode";
            return false;
        }
        break;
    case Codec::Hevc:
        context->pix_fmt = AV_PIX_FMT_YUV420P;
        SetString(context, "preset", "superfast");
        SetInt(context, "forced-idr", 1);
        if (quality && !SetInt(context, "qp", quantizer)) {
            if (!SetString(context, "x265-params", platform::Format("log-level=error:qp=%d", quantizer).c_str())) {
                error = name + " has no constant-QP mode";
                return false;
            }
        } else {
            SetString(context, "x265-params", "log-level=error");
        }
        break;
    case Codec::Av1:
        context->pix_fmt = AV_PIX_FMT_YUV420P;
        SetInt(context, "preset", 10);
        if (quality && !SetInt(context, "qp", quantizer) && !SetInt(context, "crf", quantizer)) {
            error = name + " has no constant-quantizer mode";
            return false;
        }
        break;
    }
    if (!quality) {
        ApplyBitrate(context, config.videoMbps);
        if (config.codec == Codec::Av1) context->rc_max_rate = 0;
    }
    return true;
}

bool ConfigureNvenc(const VideoEncoderConfig& config, AVCodecContext* context, const std::string& name,
                    std::string& error) {
    context->pix_fmt = AV_PIX_FMT_NV12;
    if (!SetString(context, "preset", "p4")) SetString(context, "preset", "fast");
    SetInt(context, "forced-idr", 1);
    if (config.rateControl == RateControl::Quality) {
        if (!SetString(context, "rc", "constqp") ||
            !SetInt(context, "qp", QuantizerFor(config.codec, Backend::Nvenc, config.quality))) {
            error = name + " has no constant-QP mode";
            return false;
        }
    } else {
        SetString(context, "rc", "vbr");
        ApplyBitrate(context, config.videoMbps);
    }
    return true;
}

bool ConfigureVaapi(const VideoEncoderConfig& config, AVCodecContext* context, VideoEncoder& encoder,
                    std::string& error) {
    int result = av_hwdevice_ctx_create(&encoder.device, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
    if (result < 0) {
        error = "VA-API device: " + ErrorText(result);
        return false;
    }
    encoder.frames = av_hwframe_ctx_alloc(encoder.device);
    if (!encoder.frames) {
        error = "VA-API frame pool: out of memory";
        return false;
    }
    auto* frames = reinterpret_cast<AVHWFramesContext*>(encoder.frames->data);
    frames->format = AV_PIX_FMT_VAAPI;
    frames->sw_format = AV_PIX_FMT_NV12;
    frames->width = config.width;
    frames->height = config.height;
    frames->initial_pool_size = 20;
    result = av_hwframe_ctx_init(encoder.frames);
    if (result < 0) {
        error = "VA-API frame pool: " + ErrorText(result);
        return false;
    }
    context->pix_fmt = AV_PIX_FMT_VAAPI;
    context->hw_frames_ctx = av_buffer_ref(encoder.frames);
    if (!context->hw_frames_ctx) {
        error = "VA-API frame pool: out of memory";
        return false;
    }
    if (config.rateControl == RateControl::Quality) {
        SetString(context, "rc_mode", "CQP");
        context->global_quality = QuantizerFor(config.codec, Backend::Vaapi, config.quality);
    } else {
        SetString(context, "rc_mode", "VBR");
        ApplyBitrate(context, config.videoMbps);
    }
    return true;
}

}

VideoEncoder::~VideoEncoder() {
    Close();
}

void VideoEncoder::Close() {
    avcodec_free_context(&context);
    av_buffer_unref(&frames);
    av_buffer_unref(&device);
    inputFormat = AV_PIX_FMT_NONE;
}

void InitializeLogging() {
    std::call_once(g_logging, [] {
        av_log_set_level(AV_LOG_ERROR);
        av_log_set_callback(ForwardLog);
    });
}

std::string ErrorText(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return text;
}

const char* EncoderName(Codec codec, Backend backend) {
    switch (codec) {
    case Codec::H264:
        return backend == Backend::Nvenc ? "h264_nvenc" : backend == Backend::Vaapi ? "h264_vaapi" : "libx264";
    case Codec::Hevc:
        return backend == Backend::Nvenc ? "hevc_nvenc" : backend == Backend::Vaapi ? "hevc_vaapi" : "libx265";
    case Codec::Av1:
        return backend == Backend::Nvenc ? "av1_nvenc" : backend == Backend::Vaapi ? "av1_vaapi" : "libsvtav1";
    }
    return "";
}

AVColorRange ColorRange(const video::ColorSpec& color) {
    return color.range == video::Range::Full ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
}

AVColorSpace ColorMatrix(const video::ColorSpec& color) {
    switch (color.matrix) {
    case video::Matrix::Bt601: return AVCOL_SPC_SMPTE170M;
    case video::Matrix::Bt2020: return AVCOL_SPC_BT2020_NCL;
    case video::Matrix::Bt709: break;
    }
    return AVCOL_SPC_BT709;
}

AVColorPrimaries ColorPrimaries(const video::ColorSpec& color) {
    switch (color.matrix) {
    case video::Matrix::Bt601: return AVCOL_PRI_SMPTE170M;
    case video::Matrix::Bt2020: return AVCOL_PRI_BT2020;
    case video::Matrix::Bt709: break;
    }
    return AVCOL_PRI_BT709;
}

AVColorTransferCharacteristic ColorTransfer(const video::ColorSpec& color) {
    return color.matrix == video::Matrix::Bt601 ? AVCOL_TRC_SMPTE170M : AVCOL_TRC_BT709;
}

bool OpenVideoEncoder(const VideoEncoderConfig& config, VideoEncoder& encoder, std::string& error) {
    encoder.Close();
    InitializeLogging();
    encoder.name = EncoderName(config.codec, config.backend);
    const AVCodec* codec = avcodec_find_encoder_by_name(encoder.name.c_str());
    if (!codec) {
        error = encoder.name + " is not available in this FFmpeg build";
        return false;
    }
    encoder.context = avcodec_alloc_context3(codec);
    if (!encoder.context) {
        error = encoder.name + ": out of memory";
        return false;
    }
    AVCodecContext* context = encoder.context;
    context->width = config.width;
    context->height = config.height;
    context->time_base = AVRational{1, 90000};
    context->framerate = config.frameRate;
    context->gop_size = std::max(1, static_cast<int>(std::lround(av_q2d(config.frameRate) * 2.0)));
    context->max_b_frames = 0;
    context->sample_aspect_ratio = AVRational{1, 1};
    context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    context->color_range = ColorRange(config.color);
    context->colorspace = ColorMatrix(config.color);
    context->color_primaries = ColorPrimaries(config.color);
    context->color_trc = ColorTransfer(config.color);

    bool configured = false;
    switch (config.backend) {
    case Backend::Cpu: configured = ConfigureCpu(config, context, encoder.name, error); break;
    case Backend::Nvenc: configured = ConfigureNvenc(config, context, encoder.name, error); break;
    case Backend::Vaapi: configured = ConfigureVaapi(config, context, encoder, error); break;
    }
    if (!configured) {
        encoder.Close();
        return false;
    }
    const int result = avcodec_open2(context, codec, nullptr);
    if (result < 0) {
        error = encoder.name + ": " + ErrorText(result);
        encoder.Close();
        return false;
    }
    if (context->extradata_size <= 0 || !context->extradata) {
        error = encoder.name + " provided no stream header for MP4";
        encoder.Close();
        return false;
    }
    encoder.inputFormat = config.backend == Backend::Vaapi ? AV_PIX_FMT_NV12 : context->pix_fmt;
    return true;
}

}
