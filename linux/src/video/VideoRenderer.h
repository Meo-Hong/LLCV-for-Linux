#pragma once

#include "capture/VideoFrame.h"
#include "video/GlApi.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace llcv::video {

struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

enum class OutputMode { Sdr, Pq };

class VideoRenderer {
public:
    bool Initialize(std::string& error);
    void Shutdown();
    void Configure(OutputMode output, float sdrWhiteNits);
    void Upload(const capture::VideoFrame& frame);
    void Draw(int targetWidth, int targetHeight, bool sharp);
    void BeginUiLayer(int targetWidth, int targetHeight);
    void EndUiLayer();
    void ComposeHdr(int targetWidth, int targetHeight, bool sharp);
    void Clear(int targetWidth, int targetHeight, float red, float green, float blue);
    void Reset();
    bool ReadImage(std::vector<uint8_t>& rgba, int& width, int& height);

    bool HasImage() const { return hasImage_; }
    bool SourceIsPq() const { return hasImage_ && sourceColor_.transfer == Transfer::Pq && sourceProgram_ != kRgb; }
    OutputMode Output() const { return output_; }
    int ImageWidth() const { return sourceWidth_; }
    int ImageHeight() const { return sourceHeight_; }
    PixelRect LastVideoRect() const { return lastRect_; }

private:
    enum ProgramIndex { kNv12, kP010, kYuyv, kYuv3, kRgb, kDisplay, kCompose, kProgramCount };

    struct Program {
        GLuint id = 0;
        GLint destination = -1;
        GLint flip = -1;
        GLint matrix = -1;
        GLint offset = -1;
        GLint transfer = -1;
        GLint output = -1;
        GLint sdrWhite = -1;
        GLint videoRect = -1;
        GLint hasVideo = -1;
        std::array<GLint, 3> samplers{-1, -1, -1};
    };

    struct PlaneTexture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
        GLint internalFormat = 0;
    };

    struct Target {
        GLuint texture = 0;
        GLuint framebuffer = 0;
        int width = 0;
        int height = 0;
        GLint internalFormat = 0;
    };

    bool BuildProgram(ProgramIndex index, const char* fragment, std::string& error);
    void UploadPlane(int index, const capture::FramePlane& plane, GLint internalFormat, GLenum format,
                     GLenum type, int bytesPerTexel);
    void EnsureTarget(Target& target, int width, int height, GLint internalFormat);
    void Convert(Target& target, OutputMode output);
    void SetFilter(GLuint texture, bool sharp);
    PixelRect FitRect(int targetWidth, int targetHeight) const;
    void DrawQuad(const Program& program, float x0, float y0, float x1, float y1, bool flip);

    std::array<Program, kProgramCount> programs_{};
    std::array<PlaneTexture, 3> planes_{};
    GLuint vertexArray_ = 0;
    Target video_;
    Target snapshot_;
    Target ui_;
    ProgramIndex sourceProgram_ = kNv12;
    ColorSpec sourceColor_;
    int sourceBits_ = 8;
    bool sourceMsbAligned_ = false;
    int sourceWidth_ = 0;
    int sourceHeight_ = 0;
    bool hasImage_ = false;
    OutputMode output_ = OutputMode::Sdr;
    float sdrWhite_ = 203.0f;
    PixelRect lastRect_;
};

}
