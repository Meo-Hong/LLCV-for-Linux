#include "video/VideoColor.h"

#include <linux/videodev2.h>

namespace llcv::video {
namespace {

bool IsHighDefinition(int width, int height) {
    return width >= 1280 || height > 576;
}

bool MatrixFromEncoding(uint32_t encoding, Matrix& matrix) {
    switch (encoding) {
    case V4L2_YCBCR_ENC_601:
    case V4L2_YCBCR_ENC_XV601:
        matrix = Matrix::Bt601;
        return true;
    case V4L2_YCBCR_ENC_709:
    case V4L2_YCBCR_ENC_XV709:
        matrix = Matrix::Bt709;
        return true;
    case V4L2_YCBCR_ENC_BT2020:
    case V4L2_YCBCR_ENC_BT2020_CONST_LUM:
        matrix = Matrix::Bt2020;
        return true;
    default:
        return false;
    }
}

bool MatrixFromColorspace(uint32_t colorspace, Matrix& matrix) {
    switch (colorspace) {
    case V4L2_COLORSPACE_REC709:
        matrix = Matrix::Bt709;
        return true;
    case V4L2_COLORSPACE_SMPTE170M:
    case V4L2_COLORSPACE_470_SYSTEM_M:
    case V4L2_COLORSPACE_470_SYSTEM_BG:
        matrix = Matrix::Bt601;
        return true;
    case V4L2_COLORSPACE_BT2020:
        matrix = Matrix::Bt2020;
        return true;
    default:
        return false;
    }
}

void Coefficients(Matrix matrix, double& kr, double& kb) {
    switch (matrix) {
    case Matrix::Bt601:
        kr = 0.299;
        kb = 0.114;
        break;
    case Matrix::Bt2020:
        kr = 0.2627;
        kb = 0.0593;
        break;
    default:
        kr = 0.2126;
        kb = 0.0722;
        break;
    }
}

ColorSpec ResolveSdr(const ColorInput& input) {
    ColorSpec spec;
    if (input.jpeg) {
        spec.matrix = IsHighDefinition(input.width, input.height) ? Matrix::Bt709 : Matrix::Bt601;
        spec.range = Range::Full;
        spec.matrixSource = ColorSource::Jpeg;
        spec.rangeSource = ColorSource::Jpeg;
        return spec;
    }
    if (MatrixFromEncoding(input.ycbcrEncoding, spec.matrix) ||
        MatrixFromColorspace(input.colorspace, spec.matrix)) {
        spec.matrixSource = ColorSource::Driver;
    } else {
        spec.matrix = IsHighDefinition(input.width, input.height) ? Matrix::Bt709 : Matrix::Bt601;
    }
    if (input.quantization == V4L2_QUANTIZATION_FULL_RANGE) {
        spec.range = Range::Full;
        spec.rangeSource = ColorSource::Driver;
    } else if (input.quantization == V4L2_QUANTIZATION_LIM_RANGE) {
        spec.range = Range::Limited;
        spec.rangeSource = ColorSource::Driver;
    }
    return spec;
}

void ApplyOverride(ColorSpec& spec, ColorOverride overrideMode) {
    switch (overrideMode) {
    case ColorOverride::Bt709Limited:
        spec.matrix = Matrix::Bt709;
        spec.range = Range::Limited;
        break;
    case ColorOverride::Bt709Full:
        spec.matrix = Matrix::Bt709;
        spec.range = Range::Full;
        break;
    case ColorOverride::Bt601Limited:
        spec.matrix = Matrix::Bt601;
        spec.range = Range::Limited;
        break;
    case ColorOverride::Bt601Full:
        spec.matrix = Matrix::Bt601;
        spec.range = Range::Full;
        break;
    case ColorOverride::Auto:
        return;
    }
    spec.matrixSource = ColorSource::User;
    spec.rangeSource = ColorSource::User;
}

}

ColorSpec ResolveV4l2Color(const ColorInput& input, ColorOverride overrideMode, HdrInput hdrInput) {
    if (input.rgb) return {};

    const bool driverPq = input.transferFunction == V4L2_XFER_FUNC_SMPTE2084;
    bool pq = false;
    switch (hdrInput) {
    case HdrInput::Auto: pq = input.tenBit || driverPq; break;
    case HdrInput::ForceHdr10: pq = true; break;
    case HdrInput::ForceSdr: pq = false; break;
    }

    ColorSpec spec = ResolveSdr(input);
    if (!pq) {
        ApplyOverride(spec, overrideMode);
        return spec;
    }
    spec.transfer = Transfer::Pq;
    spec.matrix = Matrix::Bt2020;
    spec.matrixSource = driverPq ? ColorSource::Driver : ColorSource::Hdr10;
    if (spec.rangeSource != ColorSource::Driver || input.tenBit) {
        spec.range = input.jpeg ? Range::Full : Range::Limited;
        spec.rangeSource = driverPq ? spec.rangeSource : ColorSource::Hdr10;
    }
    return spec;
}

YuvTransform BuildYuvTransform(const ColorSpec& spec, int bitDepth, bool msbAligned16) {
    double kr = 0.0;
    double kb = 0.0;
    Coefficients(spec.matrix, kr, kb);
    const double kg = 1.0 - kr - kb;
    const double rows[9] = {
        1.0, 0.0, 2.0 * (1.0 - kr),
        1.0, -2.0 * kb * (1.0 - kb) / kg, -2.0 * kr * (1.0 - kr) / kg,
        1.0, 2.0 * (1.0 - kb), 0.0,
    };

    const double shift = static_cast<double>(1 << (bitDepth - 8));
    const double maximum = static_cast<double>((1 << bitDepth) - 1);
    const double normalizedToCode = msbAligned16 ? 65535.0 / static_cast<double>(1 << (16 - bitDepth)) : maximum;
    const bool limited = spec.range == Range::Limited;
    const double lumaOffset = limited ? 16.0 * shift : 0.0;
    const double chromaOffset = 128.0 * shift;
    const double lumaScale = limited ? 219.0 * shift : maximum;
    const double chromaScale = limited ? 224.0 * shift : maximum;

    const double scale[3] = {
        normalizedToCode / lumaScale,
        normalizedToCode / chromaScale,
        normalizedToCode / chromaScale,
    };
    YuvTransform transform{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            transform.matrix[row * 3 + column] = static_cast<float>(rows[row * 3 + column] * scale[column]);
        }
    }
    transform.offset[0] = static_cast<float>(lumaOffset / normalizedToCode);
    transform.offset[1] = static_cast<float>(chromaOffset / normalizedToCode);
    transform.offset[2] = static_cast<float>(chromaOffset / normalizedToCode);
    return transform;
}

const char* MatrixName(Matrix matrix) {
    switch (matrix) {
    case Matrix::Bt601: return "BT.601";
    case Matrix::Bt2020: return "BT.2020";
    default: return "BT.709";
    }
}

const char* RangeName(Range range) {
    return range == Range::Full ? "Full" : "Limited";
}

const char* TransferName(Transfer transfer) {
    return transfer == Transfer::Pq ? "PQ (HDR10)" : "SDR";
}

const char* SourceName(ColorSource matrixSource, ColorSource rangeSource, bool english) {
    if (matrixSource == ColorSource::User) return english ? "manual" : "수동";
    if (matrixSource == ColorSource::Hdr10) return english ? "HDR10 default" : "HDR10 기본값";
    if (matrixSource == ColorSource::Jpeg) return english ? "JPEG default" : "JPEG 기본값";
    if (matrixSource == ColorSource::Driver && rangeSource == ColorSource::Driver) {
        return english ? "driver" : "드라이버";
    }
    if (matrixSource == ColorSource::Driver || rangeSource == ColorSource::Driver) {
        return english ? "driver + default" : "드라이버 + 기본값";
    }
    return english ? "default" : "기본값";
}

}
