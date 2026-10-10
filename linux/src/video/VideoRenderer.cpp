#include "video/VideoRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace llcv::video {
namespace {

constexpr const char* kVertexShader = R"(
uniform vec4 uDestination;
uniform float uFlipY;
out vec2 vUv;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vUv = vec2(corner.x, mix(corner.y, 1.0 - corner.y, uFlipY));
    gl_Position = vec4(mix(uDestination.x, uDestination.z, corner.x),
                       mix(uDestination.y, uDestination.w, corner.y), 0.0, 1.0);
}
)";

constexpr const char* kColorLibrary = R"(
const mat3 kBt2020To709 = mat3(1.660491, -0.124550, -0.018151,
                               -0.587641, 1.132900, -0.100579,
                               -0.072850, -0.008350, 1.118730);
const mat3 kBt709To2020 = mat3(0.627404, 0.069097, 0.016391,
                               0.329283, 0.919540, 0.088013,
                               0.043313, 0.011362, 0.895595);
const float kPqM1 = 0.1593017578125;
const float kPqM2 = 78.84375;
const float kPqC1 = 0.8359375;
const float kPqC2 = 18.8515625;
const float kPqC3 = 18.6875;
vec3 pqToNits(vec3 encoded) {
    vec3 p = pow(clamp(encoded, 0.0, 1.0), vec3(1.0 / kPqM2));
    return pow(max(p - kPqC1, 0.0) / (kPqC2 - kPqC3 * p), vec3(1.0 / kPqM1)) * 10000.0;
}
vec3 nitsToPq(vec3 nits) {
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(kPqM1));
    return pow((kPqC1 + kPqC2 * y) / (1.0 + kPqC3 * y), vec3(kPqM2));
}
vec3 srgbEncode(vec3 linear) {
    vec3 x = clamp(linear, 0.0, 1.0);
    return mix(12.92 * x, 1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), x));
}
vec3 srgbDecode(vec3 encoded) {
    vec3 c = clamp(encoded, 0.0, 1.0);
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
)";

constexpr const char* kConversionDeclarations = R"(
in vec2 vUv;
out vec4 fragColor;
uniform mat3 uMatrix;
uniform vec3 uOffset;
uniform int uTransfer;
uniform int uOutput;
uniform float uSdrWhite;
)";

constexpr const char* kConversionFunctions = R"(
vec3 toRgb(vec3 yuv) {
    return clamp(uMatrix * (yuv - uOffset), 0.0, 1.0);
}
vec3 toneMapToSdr(vec3 nits2020) {
    vec3 c = kBt2020To709 * nits2020;
    float low = min(c.r, min(c.g, c.b));
    if (low < 0.0) {
        float gray = max(0.0, dot(c, vec3(0.2126, 0.7152, 0.0722)));
        c = gray + (c - gray) * (gray / (gray - low));
    }
    float white = max(uSdrWhite, 1.0);
    float knee = white * (100.0 / 203.0);
    float headroom = white - knee;
    float peak = max(max(c.r, c.g), max(c.b, 0.0));
    float scale = 1.0 / white;
    if (peak > knee) {
        float excess = peak - knee;
        scale = (knee + headroom * (excess / (headroom + excess))) / (white * peak);
    }
    return srgbEncode(c * scale);
}
vec3 finish(vec3 rgb) {
    if (uTransfer == 1) {
        if (uOutput == 1) return rgb;
        return toneMapToSdr(pqToNits(rgb));
    }
    if (uOutput == 1) return nitsToPq(kBt709To2020 * (srgbDecode(rgb) * uSdrWhite));
    return rgb;
}
)";

constexpr const char* kSemiPlanarBody = R"(
uniform sampler2D uPlane0;
uniform sampler2D uPlane1;
void main() {
    float y = texelFetch(uPlane0, ivec2(gl_FragCoord.xy), 0).r;
    vec2 chroma = texture(uPlane1, vUv).rg;
    fragColor = vec4(finish(toRgb(vec3(y, chroma))), 1.0);
}
)";

constexpr const char* kYuyvBody = R"(
uniform sampler2D uPlane0;
void main() {
    ivec2 position = ivec2(gl_FragCoord.xy);
    int pair = position.x >> 1;
    vec4 texel = texelFetch(uPlane0, ivec2(pair, position.y), 0);
    float y = (position.x & 1) == 0 ? texel.r : texel.b;
    vec2 chroma = texel.ga;
    int lastPair = textureSize(uPlane0, 0).x - 1;
    if ((position.x & 1) == 1 && pair < lastPair) {
        chroma = 0.5 * (chroma + texelFetch(uPlane0, ivec2(pair + 1, position.y), 0).ga);
    }
    fragColor = vec4(finish(toRgb(vec3(y, chroma))), 1.0);
}
)";

constexpr const char* kYuv3Body = R"(
uniform sampler2D uPlane0;
uniform sampler2D uPlane1;
uniform sampler2D uPlane2;
void main() {
    float y = texelFetch(uPlane0, ivec2(gl_FragCoord.xy), 0).r;
    float u = texture(uPlane1, vUv).r;
    float v = texture(uPlane2, vUv).r;
    fragColor = vec4(finish(toRgb(vec3(y, u, v))), 1.0);
}
)";

constexpr const char* kRgbBody = R"(
uniform sampler2D uPlane0;
uniform int uSwapRedBlue;
void main() {
    vec3 rgb = texelFetch(uPlane0, ivec2(gl_FragCoord.xy), 0).rgb;
    if (uSwapRedBlue == 1) rgb = rgb.bgr;
    fragColor = vec4(finish(rgb), 1.0);
}
)";

constexpr const char* kDisplayShader = R"(
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uPlane0;
void main() {
    fragColor = vec4(texture(uPlane0, vUv).rgb, 1.0);
}
)";

constexpr const char* kComposeDeclarations = R"(
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uPlane0;
uniform sampler2D uPlane1;
uniform vec4 uVideoRect;
uniform int uHasVideo;
uniform float uSdrWhite;
)";

constexpr const char* kComposeBody = R"(
void main() {
    vec3 videoPq = vec3(0.0);
    if (uHasVideo == 1 && vUv.x >= uVideoRect.x && vUv.x < uVideoRect.z &&
        vUv.y >= uVideoRect.y && vUv.y < uVideoRect.w) {
        vec2 uv = (vUv - uVideoRect.xy) / (uVideoRect.zw - uVideoRect.xy);
        videoPq = texture(uPlane0, uv).rgb;
    }
    vec3 nits = pqToNits(videoPq);
    vec4 ui = texelFetch(uPlane1, ivec2(gl_FragCoord.xy), 0);
    if (ui.a > 0.0) {
        vec3 uiNits = kBt709To2020 * (srgbDecode(ui.rgb / ui.a) * uSdrWhite);
        nits = uiNits * ui.a + nits * (1.0 - ui.a);
    }
    fragColor = vec4(nitsToPq(nits), 1.0);
}
)";

std::string ConversionShader(const char* body) {
    return std::string(kConversionDeclarations) + kColorLibrary + kConversionFunctions + body;
}

std::string ComposeShader() {
    return std::string(kComposeDeclarations) + kColorLibrary + kComposeBody;
}

GLuint CompileShader(GLenum type, const std::string& text, std::string& error) {
    const GLuint shader = gl.CreateShader(type);
    const char* source = text.c_str();
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);
    GLint status = GL_FALSE;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) return shader;
    GLint length = 0;
    gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
    gl.GetShaderInfoLog(shader, length, nullptr, log.data());
    error = "shader compile failed: " + log;
    gl.DeleteShader(shader);
    return 0;
}

void ConfigureTexture(GLint filter) {
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void ResetPipelineState() {
    gl.Disable(GL_BLEND);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.Disable(GL_STENCIL_TEST);
}

GLenum TargetType(GLint internalFormat) {
    return internalFormat == GL_RGB10_A2 ? GL_UNSIGNED_INT_2_10_10_10_REV : GL_UNSIGNED_BYTE;
}

GLint TargetFormat(OutputMode output) {
    return output == OutputMode::Pq ? GL_RGB10_A2 : GL_RGBA8;
}

}

std::string VideoRenderer::ShaderHeader() const {
    if (gles_) {
        return "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
    }
    return "#version 330 core\n";
}

bool VideoRenderer::Initialize(std::string& error, bool gles) {
    gles_ = gles;
    if (gles_) {
        const auto* extensions = reinterpret_cast<const char*>(gl.GetString(GL_EXTENSIONS));
        sixteenBitTextures_ = extensions && std::strstr(extensions, "GL_EXT_texture_norm16") != nullptr;
    } else {
        sixteenBitTextures_ = true;
    }
    const std::string semiPlanar = ConversionShader(kSemiPlanarBody);
    if (!BuildProgram(kNv12, semiPlanar, error) || !BuildProgram(kP010, semiPlanar, error) ||
        !BuildProgram(kYuyv, ConversionShader(kYuyvBody), error) ||
        !BuildProgram(kYuv3, ConversionShader(kYuv3Body), error) ||
        !BuildProgram(kRgb, ConversionShader(kRgbBody), error) ||
        !BuildProgram(kDisplay, kDisplayShader, error) || !BuildProgram(kCompose, ComposeShader(), error)) {
        return false;
    }
    gl.GenVertexArrays(1, &vertexArray_);
    for (auto& plane : planes_) {
        gl.GenTextures(1, &plane.id);
        gl.BindTexture(GL_TEXTURE_2D, plane.id);
        ConfigureTexture(GL_LINEAR);
    }
    gl.BindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void VideoRenderer::Shutdown() {
    for (auto& program : programs_) {
        if (program.id) gl.DeleteProgram(program.id);
        program = {};
    }
    for (auto& plane : planes_) {
        if (plane.id) gl.DeleteTextures(1, &plane.id);
        plane = {};
    }
    for (Target* target : {&video_, &snapshot_, &ui_}) {
        if (target->texture) gl.DeleteTextures(1, &target->texture);
        if (target->framebuffer) gl.DeleteFramebuffers(1, &target->framebuffer);
        *target = {};
    }
    if (vertexArray_) gl.DeleteVertexArrays(1, &vertexArray_);
    vertexArray_ = 0;
    hasImage_ = false;
}

bool VideoRenderer::BuildProgram(ProgramIndex index, const std::string& fragment, std::string& error) {
    const std::string header = ShaderHeader();
    const GLuint vertex = CompileShader(GL_VERTEX_SHADER, header + kVertexShader, error);
    if (!vertex) return false;
    const GLuint pixel = CompileShader(GL_FRAGMENT_SHADER, header + fragment, error);
    if (!pixel) {
        gl.DeleteShader(vertex);
        return false;
    }
    const GLuint id = gl.CreateProgram();
    gl.AttachShader(id, vertex);
    gl.AttachShader(id, pixel);
    gl.LinkProgram(id);
    gl.DeleteShader(vertex);
    gl.DeleteShader(pixel);
    GLint status = GL_FALSE;
    gl.GetProgramiv(id, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint length = 0;
        gl.GetProgramiv(id, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
        gl.GetProgramInfoLog(id, length, nullptr, log.data());
        error = "shader link failed: " + log;
        gl.DeleteProgram(id);
        return false;
    }
    Program& program = programs_[index];
    program.id = id;
    program.destination = gl.GetUniformLocation(id, "uDestination");
    program.flip = gl.GetUniformLocation(id, "uFlipY");
    program.matrix = gl.GetUniformLocation(id, "uMatrix");
    program.offset = gl.GetUniformLocation(id, "uOffset");
    program.transfer = gl.GetUniformLocation(id, "uTransfer");
    program.output = gl.GetUniformLocation(id, "uOutput");
    program.sdrWhite = gl.GetUniformLocation(id, "uSdrWhite");
    program.videoRect = gl.GetUniformLocation(id, "uVideoRect");
    program.hasVideo = gl.GetUniformLocation(id, "uHasVideo");
    program.swapRedBlue = gl.GetUniformLocation(id, "uSwapRedBlue");
    program.samplers = {gl.GetUniformLocation(id, "uPlane0"), gl.GetUniformLocation(id, "uPlane1"),
                        gl.GetUniformLocation(id, "uPlane2")};
    gl.UseProgram(id);
    for (int unit = 0; unit < 3; ++unit) {
        if (program.samplers[unit] >= 0) gl.Uniform1i(program.samplers[unit], unit);
    }
    gl.UseProgram(0);
    return true;
}

void VideoRenderer::UploadPlane(int index, const capture::FramePlane& plane, GLint internalFormat, GLenum format,
                                GLenum type, int bytesPerTexel) {
    PlaneTexture& texture = planes_[index];
    gl.ActiveTexture(GL_TEXTURE0 + index);
    gl.BindTexture(GL_TEXTURE_2D, texture.id);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, plane.stride / bytesPerTexel);
    if (texture.width != plane.width || texture.height != plane.height ||
        texture.internalFormat != internalFormat) {
        gl.TexImage2D(GL_TEXTURE_2D, 0, internalFormat, plane.width, plane.height, 0, format, type,
                      plane.data.data());
        texture.width = plane.width;
        texture.height = plane.height;
        texture.internalFormat = internalFormat;
    } else {
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, plane.width, plane.height, format, type, plane.data.data());
    }
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void VideoRenderer::EnsureTarget(Target& target, int width, int height, GLint internalFormat) {
    if (!target.texture) {
        gl.GenTextures(1, &target.texture);
        gl.GenFramebuffers(1, &target.framebuffer);
    }
    if (target.width == width && target.height == height && target.internalFormat == internalFormat) return;
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, target.texture);
    gl.TexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, TargetType(internalFormat), nullptr);
    ConfigureTexture(GL_LINEAR);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    target.width = width;
    target.height = height;
    target.internalFormat = internalFormat;
}

void VideoRenderer::SetFilter(GLuint texture, bool sharp) {
    gl.BindTexture(GL_TEXTURE_2D, texture);
    ConfigureTexture(sharp ? GL_NEAREST : GL_LINEAR);
}

void VideoRenderer::DrawQuad(const Program& program, float x0, float y0, float x1, float y1, bool flip) {
    gl.UseProgram(program.id);
    gl.Uniform4f(program.destination, x0, y0, x1, y1);
    gl.Uniform1f(program.flip, flip ? 1.0f : 0.0f);
    gl.BindVertexArray(vertexArray_);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.BindVertexArray(0);
    gl.UseProgram(0);
}

PixelRect VideoRenderer::FitRect(int targetWidth, int targetHeight) const {
    PixelRect rect;
    if (!hasImage_ || targetWidth <= 0 || targetHeight <= 0 || sourceWidth_ <= 0 || sourceHeight_ <= 0) {
        return rect;
    }
    const double scale = std::min(static_cast<double>(targetWidth) / sourceWidth_,
                                  static_cast<double>(targetHeight) / sourceHeight_);
    rect.width = std::min(targetWidth, std::max(1, static_cast<int>(std::lround(sourceWidth_ * scale))));
    rect.height = std::min(targetHeight, std::max(1, static_cast<int>(std::lround(sourceHeight_ * scale))));
    rect.x = (targetWidth - rect.width) / 2;
    rect.y = (targetHeight - rect.height) / 2;
    return rect;
}

void VideoRenderer::Configure(OutputMode output, float sdrWhiteNits) {
    const bool changed = output != output_ || std::abs(sdrWhiteNits - sdrWhite_) > 0.01f;
    output_ = output;
    sdrWhite_ = sdrWhiteNits;
    if (!changed || !hasImage_) return;
    EnsureTarget(video_, sourceWidth_, sourceHeight_, TargetFormat(output_));
    Convert(video_, output_);
}

void VideoRenderer::Convert(Target& target, OutputMode output) {
    const Program& program = programs_[sourceProgram_];
    gl.UseProgram(program.id);
    if (program.matrix >= 0) {
        const YuvTransform transform = BuildYuvTransform(sourceColor_, sourceBits_, sourceMsbAligned_);
        gl.UniformMatrix3fv(program.matrix, 1, GL_TRUE, transform.matrix);
        gl.Uniform3fv(program.offset, 1, transform.offset);
    }
    const bool pq = sourceColor_.transfer == Transfer::Pq && sourceProgram_ != kRgb;
    if (program.transfer >= 0) gl.Uniform1i(program.transfer, pq ? 1 : 0);
    if (program.output >= 0) gl.Uniform1i(program.output, output == OutputMode::Pq ? 1 : 0);
    if (program.sdrWhite >= 0) gl.Uniform1f(program.sdrWhite, sdrWhite_);
    if (program.swapRedBlue >= 0) gl.Uniform1i(program.swapRedBlue, sourceBgr_ ? 1 : 0);
    for (int unit = 0; unit < 3; ++unit) {
        gl.ActiveTexture(GL_TEXTURE0 + unit);
        gl.BindTexture(GL_TEXTURE_2D, planes_[unit].id);
    }
    ResetPipelineState();
    gl.BindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    gl.Viewport(0, 0, target.width, target.height);
    DrawQuad(program, -1.0f, -1.0f, 1.0f, 1.0f, false);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    for (int unit = 2; unit >= 0; --unit) {
        gl.ActiveTexture(GL_TEXTURE0 + unit);
        gl.BindTexture(GL_TEXTURE_2D, 0);
    }
}

void VideoRenderer::Upload(const capture::VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0) return;
    if (frame.layout == capture::FrameLayout::P010 && !sixteenBitTextures_) return;
    sourceBits_ = 8;
    sourceMsbAligned_ = false;
    sourceBgr_ = false;
    switch (frame.layout) {
    case capture::FrameLayout::Nv12:
        UploadPlane(0, frame.planes[0], GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1);
        UploadPlane(1, frame.planes[1], GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2);
        sourceProgram_ = kNv12;
        break;
    case capture::FrameLayout::P010:
        UploadPlane(0, frame.planes[0], GL_R16, GL_RED, GL_UNSIGNED_SHORT, 2);
        UploadPlane(1, frame.planes[1], GL_RG16, GL_RG, GL_UNSIGNED_SHORT, 4);
        sourceProgram_ = kP010;
        sourceBits_ = 10;
        sourceMsbAligned_ = true;
        break;
    case capture::FrameLayout::Yuyv:
        UploadPlane(0, frame.planes[0], GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4);
        sourceProgram_ = kYuyv;
        break;
    case capture::FrameLayout::Yuv3Plane:
        UploadPlane(0, frame.planes[0], GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1);
        UploadPlane(1, frame.planes[1], GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1);
        UploadPlane(2, frame.planes[2], GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1);
        sourceProgram_ = kYuv3;
        break;
    case capture::FrameLayout::Bgr24:
        UploadPlane(0, frame.planes[0], GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 3);
        sourceProgram_ = kRgb;
        sourceBgr_ = true;
        break;
    case capture::FrameLayout::Rgb24:
        UploadPlane(0, frame.planes[0], GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 3);
        sourceProgram_ = kRgb;
        break;
    }
    sourceColor_ = frame.color;
    sourceWidth_ = frame.width;
    sourceHeight_ = frame.height;
    EnsureTarget(video_, sourceWidth_, sourceHeight_, TargetFormat(output_));
    Convert(video_, output_);
    hasImage_ = true;
}

void VideoRenderer::Clear(int targetWidth, int targetHeight, float red, float green, float blue) {
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, targetWidth, targetHeight);
    gl.ClearColor(red, green, blue, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT);
}

void VideoRenderer::Draw(int targetWidth, int targetHeight, bool sharp) {
    Clear(targetWidth, targetHeight, 0.0f, 0.0f, 0.0f);
    lastRect_ = FitRect(targetWidth, targetHeight);
    if (lastRect_.width <= 0 || lastRect_.height <= 0) return;

    gl.ActiveTexture(GL_TEXTURE0);
    SetFilter(video_.texture, sharp);
    ResetPipelineState();
    const PixelRect& rect = lastRect_;
    const float x0 = static_cast<float>(rect.x) / targetWidth * 2.0f - 1.0f;
    const float x1 = static_cast<float>(rect.x + rect.width) / targetWidth * 2.0f - 1.0f;
    const float yTop = 1.0f - static_cast<float>(rect.y) / targetHeight * 2.0f;
    const float yBottom = 1.0f - static_cast<float>(rect.y + rect.height) / targetHeight * 2.0f;
    DrawQuad(programs_[kDisplay], x0, yBottom, x1, yTop, true);
    gl.BindTexture(GL_TEXTURE_2D, 0);
}

void VideoRenderer::BeginUiLayer(int targetWidth, int targetHeight) {
    EnsureTarget(ui_, std::max(1, targetWidth), std::max(1, targetHeight), GL_RGBA8);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ui_.framebuffer);
    gl.Viewport(0, 0, ui_.width, ui_.height);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT);
}

void VideoRenderer::EndUiLayer() {
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void VideoRenderer::ComposeHdr(int targetWidth, int targetHeight, bool sharp) {
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, targetWidth, targetHeight);
    lastRect_ = FitRect(targetWidth, targetHeight);
    const bool video = output_ == OutputMode::Pq && lastRect_.width > 0 && lastRect_.height > 0;

    gl.ActiveTexture(GL_TEXTURE0);
    if (video) {
        SetFilter(video_.texture, sharp);
    } else {
        gl.BindTexture(GL_TEXTURE_2D, 0);
    }
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, ui_.texture);

    const Program& program = programs_[kCompose];
    gl.UseProgram(program.id);
    const float width = static_cast<float>(std::max(targetWidth, 1));
    const float height = static_cast<float>(std::max(targetHeight, 1));
    gl.Uniform4f(program.videoRect, lastRect_.x / width, lastRect_.y / height,
                 (lastRect_.x + lastRect_.width) / width, (lastRect_.y + lastRect_.height) / height);
    gl.Uniform1i(program.hasVideo, video ? 1 : 0);
    gl.Uniform1f(program.sdrWhite, sdrWhite_);
    ResetPipelineState();
    DrawQuad(program, -1.0f, -1.0f, 1.0f, 1.0f, true);

    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, 0);
}

void VideoRenderer::Reset() {
    hasImage_ = false;
    lastRect_ = {};
}

bool VideoRenderer::ReadImage(std::vector<uint8_t>& rgba, int& width, int& height) {
    if (!hasImage_ || sourceWidth_ <= 0 || sourceHeight_ <= 0) return false;
    Target* source = &video_;
    if (output_ != OutputMode::Sdr) {
        EnsureTarget(snapshot_, sourceWidth_, sourceHeight_, GL_RGBA8);
        Convert(snapshot_, OutputMode::Sdr);
        source = &snapshot_;
    }
    width = source->width;
    height = source->height;
    rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    gl.BindFramebuffer(GL_FRAMEBUFFER, source->framebuffer);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

}
