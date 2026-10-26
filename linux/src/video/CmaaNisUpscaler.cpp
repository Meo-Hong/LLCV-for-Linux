#include "video/CmaaNisUpscaler.h"

#include "diagnostics/Logger.h"
#include "platform/Strings.h"
#include "UpscalingShaders.h"

#include "nis/NIS_Config.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace llcv::video {
namespace {

constexpr int kMaximumSize = 4096;
constexpr GLuint kEdgeBlock = 28;
constexpr GLuint kTransferGroup = 16;
constexpr GLuint kNisBlockWidth = 32;
constexpr GLuint kNisBlockHeight = 24;

constexpr const char* kShaderVersion = "#version 430\n";

constexpr const char* kTransferShader = R"(
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(binding = 0) uniform sampler2D uSource;
layout(binding = 0, rgba16f) uniform writeonly image2D uTarget;
float Decode709(float v) { return v < 0.081 ? v / 4.5 : pow((v + 0.099) / 1.099, 1.0 / 0.45); }
float Encode709(float v) { return v < 0.018 ? v * 4.5 : 1.099 * pow(v, 0.45) - 0.099; }
void main() {
    ivec2 position = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(position, imageSize(uTarget)))) return;
    vec3 c = clamp(texelFetch(uSource, position, 0).rgb, 0.0, 1.0);
#ifdef DECODE
    imageStore(uTarget, position, vec4(Decode709(c.r), Decode709(c.g), Decode709(c.b), 1.0));
#else
    imageStore(uTarget, position, vec4(Encode709(c.r), Encode709(c.g), Encode709(c.b), 1.0));
#endif
}
)";

constexpr const char* kNisDeclarations = R"(
#define NIS_HLSL 0
#define NIS_HLSL_6_2 0
#define NIS_GLSL 1
#define NIS_SCALER 1
#define NIS_HDR_MODE 0
#define NIS_NV12_SUPPORT 0
#define NIS_VIEWPORT_SUPPORT 0
#define NIS_CLAMP_OUTPUT 1
#define NIS_USE_HALF_PRECISION 0
#define NIS_TEXTURE_GATHER 0
#define NIS_BLOCK_WIDTH 32
#define NIS_BLOCK_HEIGHT 24
layout(std140, binding = 0) uniform const_buffer {
    float kDetectRatio;
    float kDetectThres;
    float kMinContrastRatio;
    float kRatioNorm;
    float kContrastBoost;
    float kEps;
    float kSharpStartY;
    float kSharpScaleY;
    float kSharpStrengthMin;
    float kSharpStrengthScale;
    float kSharpLimitMin;
    float kSharpLimitScale;
    float kScaleX;
    float kScaleY;
    float kDstNormX;
    float kDstNormY;
    float kSrcNormX;
    float kSrcNormY;
    uint kInputViewportOriginX;
    uint kInputViewportOriginY;
    uint kInputViewportWidth;
    uint kInputViewportHeight;
    uint kOutputViewportOriginX;
    uint kOutputViewportOriginY;
    uint kOutputViewportWidth;
    uint kOutputViewportHeight;
    float reserved0;
    float reserved1;
};
layout(binding = 0) uniform sampler2D in_texture;
layout(binding = 1) uniform sampler2D coef_scaler;
layout(binding = 2) uniform sampler2D coef_usm;
layout(binding = 0, rgba16f) uniform writeonly image2D out_texture;
)";

constexpr const char* kNisMain = R"(
layout(local_size_x = NIS_THREAD_GROUP_SIZE, local_size_y = 1, local_size_z = 1) in;
void main() {
    NVScaler(gl_WorkGroupID.xy, gl_LocalInvocationID.x);
}
)";

GLuint BuildCompute(const char* name, const std::string& source, std::string& error) {
    const GLuint shader = gl.CreateShader(GL_COMPUTE_SHADER);
    const char* text = source.c_str();
    gl.ShaderSource(shader, 1, &text, nullptr);
    gl.CompileShader(shader);
    GLint status = GL_FALSE;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint length = 0;
        gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
        gl.GetShaderInfoLog(shader, length, nullptr, log.data());
        error = platform::Format("%s shader compile failed: %s", name, log.c_str());
        gl.DeleteShader(shader);
        return 0;
    }
    const GLuint program = gl.CreateProgram();
    gl.AttachShader(program, shader);
    gl.LinkProgram(program);
    gl.DeleteShader(shader);
    gl.GetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint length = 0;
        gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
        gl.GetProgramInfoLog(program, length, nullptr, log.data());
        error = platform::Format("%s shader link failed: %s", name, log.c_str());
        gl.DeleteProgram(program);
        return 0;
    }
    return program;
}

void CreateTexture(GLuint& texture, int width, int height, GLint internalFormat, GLenum format, GLenum type,
                   GLint filter, const void* data = nullptr) {
    if (!texture) gl.GenTextures(1, &texture);
    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.TexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, data);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.BindTexture(GL_TEXTURE_2D, 0);
}

void CreateStorage(GLuint& buffer, size_t bytes) {
    if (!buffer) gl.GenBuffers(1, &buffer);
    gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    gl.BufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_DYNAMIC_DRAW);
    gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void DeleteTexture(GLuint& texture) {
    if (texture) gl.DeleteTextures(1, &texture);
    texture = 0;
}

void DeleteBuffer(GLuint& buffer) {
    if (buffer) gl.DeleteBuffers(1, &buffer);
    buffer = 0;
}

void DeleteProgram(GLuint& program) {
    if (program) gl.DeleteProgram(program);
    program = 0;
}

GLuint Groups(int size, GLuint block) {
    return (static_cast<GLuint>(size) + block - 1) / block;
}

}

bool UpscalingEligible(int width, int height, int outputWidth, int outputHeight) {
    if (width <= 0 || height <= 0 || width > kMaximumSize || height > kMaximumSize) return false;
    return outputWidth >= width && outputHeight >= height && (outputWidth > width || outputHeight > height) &&
           outputWidth <= 2 * width && outputHeight <= 2 * height;
}

bool CmaaNisUpscaler::ContextSupported(bool gles, std::string& reason) {
    const auto* version = reinterpret_cast<const char*>(gl.GetString(GL_VERSION));
    if (gles) {
        reason = platform::Format("OpenGL ES (%s)", version ? version : "?");
        return false;
    }
    std::string missing;
    if (!LoadComputeApi(missing)) {
        reason = "missing " + missing;
        return false;
    }
    int major = 0;
    int minor = 0;
    if (version && std::sscanf(version, "%d.%d", &major, &minor) == 2 && (major > 4 || (major == 4 && minor >= 3))) {
        return true;
    }
    bool compute = false;
    bool images = false;
    bool storage = false;
    GLint count = 0;
    gl.GetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* name = reinterpret_cast<const char*>(gl.GetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (!name) continue;
        compute = compute || std::strcmp(name, "GL_ARB_compute_shader") == 0;
        images = images || std::strcmp(name, "GL_ARB_shader_image_load_store") == 0;
        storage = storage || std::strcmp(name, "GL_ARB_shader_storage_buffer_object") == 0;
    }
    if (compute && images && storage) return true;
    reason = platform::Format("OpenGL %d.%d without compute shaders", major, minor);
    return false;
}

void CmaaNisUpscaler::Fail(const std::string& error) {
    failed_ = true;
    error_ = error;
    diagnostics::Log("[upscaling] CMAA2 -> NIS unavailable: %s; using the normal path", error.c_str());
}

bool CmaaNisUpscaler::Initialize() {
    initialized_ = true;
    std::string missing;
    if (!LoadComputeApi(missing)) {
        Fail("missing " + missing);
        return false;
    }
    const auto* vendor = reinterpret_cast<const char*>(gl.GetString(GL_VENDOR));
    const bool nvidia = vendor && std::strstr(vendor, "NVIDIA") != nullptr;
    const std::string cmaa(shaders::kCmaa2Glsl);
    const std::string transfer(kTransferShader);
    std::string error;
    decode_ = BuildCompute("decode", std::string(kShaderVersion) + "#define DECODE 1\n" + transfer, error);
    if (decode_) encode_ = BuildCompute("encode", std::string(kShaderVersion) + transfer, error);
    if (encode_) {
        edges_ = BuildCompute("CMAA2 edges", std::string(kShaderVersion) + "#define CMAA2_PASS_EDGES 1\n" + cmaa,
                              error);
    }
    if (edges_) {
        dispatchArgs_ = BuildCompute("CMAA2 dispatch",
                                     std::string(kShaderVersion) + "#define CMAA2_PASS_DISPATCH_ARGS 1\n" + cmaa, error);
    }
    if (dispatchArgs_) {
        candidates_ = BuildCompute(
            "CMAA2 candidates", std::string(kShaderVersion) + "#define CMAA2_PASS_PROCESS_CANDIDATES 1\n" + cmaa, error);
    }
    if (candidates_) {
        apply_ = BuildCompute("CMAA2 apply", std::string(kShaderVersion) + "#define CMAA2_PASS_DEFERRED_APPLY 1\n" + cmaa,
                              error);
    }
    if (apply_) {
        nis_ = BuildCompute("NIS",
                            std::string(kShaderVersion) +
                                platform::Format("#define NIS_THREAD_GROUP_SIZE %d\n", nvidia ? 128 : 256) +
                                kNisDeclarations + shaders::kNisScalerGlsl + kNisMain,
                            error);
    }
    if (!nis_) {
        Fail(error);
        return false;
    }
    CreateTexture(coefScale_, 2, static_cast<int>(kPhaseCount), GL_RGBA32F, GL_RGBA, GL_FLOAT, GL_NEAREST, coef_scale);
    CreateTexture(coefUsm_, 2, static_cast<int>(kPhaseCount), GL_RGBA32F, GL_RGBA, GL_FLOAT, GL_NEAREST, coef_usm);
    gl.GenBuffers(1, &nisConfig_);
    gl.BindBuffer(GL_UNIFORM_BUFFER, nisConfig_);
    gl.BufferData(GL_UNIFORM_BUFFER, sizeof(NISConfig), nullptr, GL_DYNAMIC_DRAW);
    gl.BindBuffer(GL_UNIFORM_BUFFER, 0);
    CreateStorage(control_, 16 * sizeof(uint32_t));
    CreateStorage(indirect_, 4 * sizeof(uint32_t));
    diagnostics::Log("[upscaling] CMAA2 -> NIS shaders ready (NIS group %d, %s)", nvidia ? 128 : 256,
                     vendor ? vendor : "?");
    return true;
}

bool CmaaNisUpscaler::EnsureSource(int width, int height) {
    if (linear_.width == width && linear_.height == height) return true;
    const int halfWidth = (width + 1) / 2;
    const int halfHeight = (height + 1) / 2;
    CreateTexture(linear_.texture, width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST);
    CreateTexture(encoded_.texture, width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    CreateTexture(edgeImage_.texture, halfWidth, height, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, GL_NEAREST);
    CreateTexture(heads_.texture, halfWidth, halfHeight, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, GL_NEAREST);
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t quads = static_cast<size_t>(halfWidth) * static_cast<size_t>(halfHeight);
    CreateStorage(shapeCandidates_, pixels * sizeof(uint32_t));
    CreateStorage(blendLocations_, quads * sizeof(uint32_t));
    CreateStorage(blendItems_, pixels * 2 * sizeof(uint32_t));
    linear_.width = encoded_.width = edgeImage_.width = width;
    linear_.height = encoded_.height = edgeImage_.height = heads_.height = height;
    heads_.width = halfWidth;
    processed_ = false;
    diagnostics::Log("[upscaling] CMAA2 work buffers %dx%d", width, height);
    return true;
}

bool CmaaNisUpscaler::EnsureOutput(int width, int height, float sharpness) {
    if (output_.width == width && output_.height == height && sharpness == sharpness_) return true;
    NISConfig config{};
    if (!NVScalerUpdateConfig(config, sharpness, 0, 0, static_cast<uint32_t>(linear_.width),
                              static_cast<uint32_t>(linear_.height), static_cast<uint32_t>(linear_.width),
                              static_cast<uint32_t>(linear_.height), 0, 0, static_cast<uint32_t>(width),
                              static_cast<uint32_t>(height), static_cast<uint32_t>(width),
                              static_cast<uint32_t>(height), NISHDRMode::None)) {
        return false;
    }
    gl.BindBuffer(GL_UNIFORM_BUFFER, nisConfig_);
    gl.BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(config), &config);
    gl.BindBuffer(GL_UNIFORM_BUFFER, 0);
    if (output_.width != width || output_.height != height) {
        CreateTexture(output_.texture, width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST);
        output_.width = width;
        output_.height = height;
    }
    sharpness_ = sharpness;
    processed_ = false;
    return true;
}

GLuint CmaaNisUpscaler::Process(GLuint source, int width, int height, int outputWidth, int outputHeight,
                                float sharpness, uint64_t generation) {
    if (failed_ || !UpscalingEligible(width, height, outputWidth, outputHeight)) return 0;
    if (!initialized_ && !Initialize()) return 0;
    const bool sourceChanged = linear_.width != width || linear_.height != height;
    if (!EnsureSource(width, height)) return 0;
    if (sourceChanged) output_.width = output_.height = 0;
    if (!EnsureOutput(outputWidth, outputHeight, std::clamp(sharpness, 0.0f, 1.0f))) return 0;
    if (processed_ && processedGeneration_ == generation) return output_.texture;
    Run(source);
    processed_ = true;
    processedGeneration_ = generation;
    return output_.texture;
}

void CmaaNisUpscaler::Run(GLuint source) {
    const int width = linear_.width;
    const int height = linear_.height;
    static constexpr uint32_t kZeros[16] = {};

    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, source);
    gl.BindImageTexture(0, linear_.texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl.UseProgram(decode_);
    gl.DispatchCompute(Groups(width, kTransferGroup), Groups(height, kTransferGroup), 1);
    gl.MemoryBarrier(GL_ALL_BARRIER_BITS);

    gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, control_);
    gl.BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(kZeros), kZeros);
    gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    gl.BindTexture(GL_TEXTURE_2D, linear_.texture);
    gl.BindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl.BindImageTexture(1, edgeImage_.texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R8UI);
    gl.BindImageTexture(5, heads_.texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, shapeCandidates_);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, blendLocations_);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, blendItems_);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, control_);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, indirect_);
    gl.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, indirect_);
    gl.MemoryBarrier(GL_ALL_BARRIER_BITS);

    gl.UseProgram(edges_);
    gl.DispatchCompute(Groups(width, kEdgeBlock), Groups(height, kEdgeBlock), 1);
    gl.MemoryBarrier(GL_ALL_BARRIER_BITS);
    gl.UseProgram(dispatchArgs_);
    gl.DispatchCompute(2, 1, 1);
    gl.MemoryBarrier(GL_ALL_BARRIER_BITS);
    gl.UseProgram(candidates_);
    gl.DispatchComputeIndirect(0);
    gl.MemoryBarrier(GL_ALL_BARRIER_BITS);
    gl.UseProgram(dispatchArgs_);
    gl.DispatchCompute(1, 2, 1);
    gl.MemoryBarrier(GL_ALL_BARRIER_BITS);

    gl.BindTexture(GL_TEXTURE_2D, 0);
    gl.BindImageTexture(0, linear_.texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl.UseProgram(apply_);
    gl.DispatchComputeIndirect(0);
    gl.MemoryBarrier(GL_ALL_BARRIER_BITS);

    gl.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    for (const GLuint binding : {2u, 3u, 4u, 6u, 7u}) gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, 0);
    gl.BindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R8UI);
    gl.BindImageTexture(5, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

    gl.BindTexture(GL_TEXTURE_2D, linear_.texture);
    gl.BindImageTexture(0, encoded_.texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl.UseProgram(encode_);
    gl.DispatchCompute(Groups(width, kTransferGroup), Groups(height, kTransferGroup), 1);
    gl.MemoryBarrier(GL_ALL_BARRIER_BITS);

    gl.BindTexture(GL_TEXTURE_2D, encoded_.texture);
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, coefScale_);
    gl.ActiveTexture(GL_TEXTURE2);
    gl.BindTexture(GL_TEXTURE_2D, coefUsm_);
    gl.BindBufferBase(GL_UNIFORM_BUFFER, 0, nisConfig_);
    gl.BindImageTexture(0, output_.texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl.UseProgram(nis_);
    gl.DispatchCompute(Groups(output_.width, kNisBlockWidth), Groups(output_.height, kNisBlockHeight), 1);
    gl.MemoryBarrier(GL_ALL_BARRIER_BITS);

    gl.UseProgram(0);
    gl.BindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
    gl.BindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, 0);
}

void CmaaNisUpscaler::ReleaseSource() {
    DeleteTexture(linear_.texture);
    DeleteTexture(encoded_.texture);
    DeleteTexture(edgeImage_.texture);
    DeleteTexture(heads_.texture);
    DeleteTexture(output_.texture);
    DeleteBuffer(shapeCandidates_);
    DeleteBuffer(blendLocations_);
    DeleteBuffer(blendItems_);
    linear_ = encoded_ = edgeImage_ = heads_ = output_ = {};
}

void CmaaNisUpscaler::Shutdown() {
    ReleaseSource();
    DeleteBuffer(control_);
    DeleteBuffer(indirect_);
    DeleteBuffer(nisConfig_);
    DeleteTexture(coefScale_);
    DeleteTexture(coefUsm_);
    for (GLuint* program : {&decode_, &encode_, &edges_, &dispatchArgs_, &candidates_, &apply_, &nis_}) {
        DeleteProgram(*program);
    }
    initialized_ = false;
    failed_ = false;
    error_.clear();
    sharpness_ = -1.0f;
    processed_ = false;
}

}
