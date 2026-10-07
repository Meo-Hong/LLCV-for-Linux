#pragma once

#include <cstdint>

namespace llcv::video {

enum class Matrix : uint8_t { Bt601, Bt709, Bt2020 };
enum class Range : uint8_t { Limited, Full };
enum class Transfer : uint8_t { Sdr, Pq };
enum class ColorSource : uint8_t { Driver, Default, Jpeg, User, Hdr10 };
enum class ColorOverride : uint8_t { Auto, Bt709Limited, Bt709Full, Bt601Limited, Bt601Full };
enum class HdrInput : uint8_t { Auto, ForceHdr10, ForceSdr };

struct ColorSpec {
    Matrix matrix = Matrix::Bt709;
    Range range = Range::Limited;
    Transfer transfer = Transfer::Sdr;
    ColorSource matrixSource = ColorSource::Default;
    ColorSource rangeSource = ColorSource::Default;
};

struct ColorInput {
    uint32_t colorspace = 0;
    uint32_t ycbcrEncoding = 0;
    uint32_t quantization = 0;
    uint32_t transferFunction = 0;
    bool jpeg = false;
    bool rgb = false;
    bool tenBit = false;
    int width = 0;
    int height = 0;
};

struct YuvTransform {
    float matrix[9];
    float offset[3];
};

ColorSpec ResolveV4l2Color(const ColorInput& input, ColorOverride overrideMode, HdrInput hdrInput);
YuvTransform BuildYuvTransform(const ColorSpec& spec, int bitDepth, bool msbAligned16);
const char* MatrixName(Matrix matrix);
const char* RangeName(Range range);
const char* TransferName(Transfer transfer);
const char* SourceName(ColorSource matrixSource, ColorSource rangeSource, bool english);

}
