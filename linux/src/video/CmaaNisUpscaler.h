#pragma once

#include "video/GlApi.h"

#include <cstdint>
#include <string>

namespace llcv::video {

enum class UpscaleState { Off, Active, ActiveHdr, ScaleRange, Unavailable };

bool UpscalingEligible(int width, int height, int outputWidth, int outputHeight);

class CmaaNisUpscaler {
public:
    static bool ContextSupported(bool gles, std::string& reason);

    void Shutdown();
    GLuint Process(GLuint source, int width, int height, int outputWidth, int outputHeight, float sharpness,
                   bool preserve, bool hdr, uint64_t generation);
    bool Failed() const { return failed_; }
    const std::string& Error() const { return error_; }

private:
    struct Image {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
    };

    struct CmaaPrograms {
        GLuint edges = 0;
        GLuint dispatchArgs = 0;
        GLuint candidates = 0;
        GLuint apply = 0;
    };

    bool Initialize();
    bool BuildCmaa(CmaaPrograms& programs, bool preserve, std::string& error);
    GLuint BuildNis(bool hdr, std::string& error) const;
    bool EnsurePrograms(bool hdr, bool preserve);
    bool EnsureSource(int width, int height);
    bool EnsureOutput(int width, int height, float sharpness, bool hdr);
    void ReleaseSource();
    void Run(GLuint source, const CmaaPrograms& cmaa);
    void RunHdr(GLuint source);
    void RunNis(GLuint program, GLuint input);
    void Fail(const std::string& error);

    bool initialized_ = false;
    bool failed_ = false;
    std::string error_;
    int nisGroup_ = 256;
    GLuint decode_ = 0;
    GLuint encode_ = 0;
    CmaaPrograms high_;
    CmaaPrograms preserve_;
    GLuint nis_ = 0;
    GLuint nisPq_ = 0;
    GLuint guard_ = 0;
    GLuint coefScale_ = 0;
    GLuint coefUsm_ = 0;
    GLuint nisConfig_ = 0;
    int sourceWidth_ = 0;
    int sourceHeight_ = 0;
    Image linear_;
    Image encoded_;
    Image edgeImage_;
    Image heads_;
    Image output_;
    Image guarded_;
    GLuint shapeCandidates_ = 0;
    GLuint blendLocations_ = 0;
    GLuint blendItems_ = 0;
    GLuint control_ = 0;
    GLuint indirect_ = 0;
    float sharpness_ = -1.0f;
    bool configHdr_ = false;
    uint64_t processedGeneration_ = 0;
    bool processedPreserve_ = false;
    bool processed_ = false;
};

}
