#pragma once

#include "video/GlApi.h"

#include <cstdint>
#include <string>

namespace llcv::video {

enum class UpscaleState { Off, Active, SdrOnly, ScaleRange, Unavailable };

bool UpscalingEligible(int width, int height, int outputWidth, int outputHeight);

class CmaaNisUpscaler {
public:
    static bool ContextSupported(bool gles, std::string& reason);

    void Shutdown();
    GLuint Process(GLuint source, int width, int height, int outputWidth, int outputHeight, float sharpness,
                   uint64_t generation);
    bool Failed() const { return failed_; }
    const std::string& Error() const { return error_; }

private:
    struct Image {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
    };

    bool Initialize();
    bool EnsureSource(int width, int height);
    bool EnsureOutput(int width, int height, float sharpness);
    void ReleaseSource();
    void Run(GLuint source);
    void Fail(const std::string& error);

    bool initialized_ = false;
    bool failed_ = false;
    std::string error_;
    GLuint decode_ = 0;
    GLuint encode_ = 0;
    GLuint edges_ = 0;
    GLuint dispatchArgs_ = 0;
    GLuint candidates_ = 0;
    GLuint apply_ = 0;
    GLuint nis_ = 0;
    GLuint coefScale_ = 0;
    GLuint coefUsm_ = 0;
    GLuint nisConfig_ = 0;
    Image linear_;
    Image encoded_;
    Image edgeImage_;
    Image heads_;
    Image output_;
    GLuint shapeCandidates_ = 0;
    GLuint blendLocations_ = 0;
    GLuint blendItems_ = 0;
    GLuint control_ = 0;
    GLuint indirect_ = 0;
    float sharpness_ = -1.0f;
    uint64_t processedGeneration_ = 0;
    bool processed_ = false;
};

}
