#include "video/GlApi.h"

#include <SDL3/SDL_video.h>

namespace llcv::video {

GlApi gl{};

namespace {

template <typename Function>
bool Resolve(Function& target, const char* name, std::string& missing) {
    target = reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
    if (!target) missing = name;
    return target != nullptr;
}

}

bool LoadGlApi(std::string& missing) {
    return Resolve(gl.ActiveTexture, "glActiveTexture", missing) &&
           Resolve(gl.AttachShader, "glAttachShader", missing) &&
           Resolve(gl.BindFramebuffer, "glBindFramebuffer", missing) &&
           Resolve(gl.BindTexture, "glBindTexture", missing) &&
           Resolve(gl.BindVertexArray, "glBindVertexArray", missing) &&
           Resolve(gl.CheckFramebufferStatus, "glCheckFramebufferStatus", missing) &&
           Resolve(gl.Clear, "glClear", missing) &&
           Resolve(gl.ClearColor, "glClearColor", missing) &&
           Resolve(gl.CompileShader, "glCompileShader", missing) &&
           Resolve(gl.CreateProgram, "glCreateProgram", missing) &&
           Resolve(gl.CreateShader, "glCreateShader", missing) &&
           Resolve(gl.DeleteFramebuffers, "glDeleteFramebuffers", missing) &&
           Resolve(gl.DeleteProgram, "glDeleteProgram", missing) &&
           Resolve(gl.DeleteShader, "glDeleteShader", missing) &&
           Resolve(gl.DeleteTextures, "glDeleteTextures", missing) &&
           Resolve(gl.DeleteVertexArrays, "glDeleteVertexArrays", missing) &&
           Resolve(gl.Disable, "glDisable", missing) &&
           Resolve(gl.DrawArrays, "glDrawArrays", missing) &&
           Resolve(gl.FramebufferTexture2D, "glFramebufferTexture2D", missing) &&
           Resolve(gl.GenFramebuffers, "glGenFramebuffers", missing) &&
           Resolve(gl.GenTextures, "glGenTextures", missing) &&
           Resolve(gl.GenVertexArrays, "glGenVertexArrays", missing) &&
           Resolve(gl.GetProgramInfoLog, "glGetProgramInfoLog", missing) &&
           Resolve(gl.GetProgramiv, "glGetProgramiv", missing) &&
           Resolve(gl.GetShaderInfoLog, "glGetShaderInfoLog", missing) &&
           Resolve(gl.GetShaderiv, "glGetShaderiv", missing) &&
           Resolve(gl.GetString, "glGetString", missing) &&
           Resolve(gl.GetUniformLocation, "glGetUniformLocation", missing) &&
           Resolve(gl.LinkProgram, "glLinkProgram", missing) &&
           Resolve(gl.PixelStorei, "glPixelStorei", missing) &&
           Resolve(gl.ReadPixels, "glReadPixels", missing) &&
           Resolve(gl.ShaderSource, "glShaderSource", missing) &&
           Resolve(gl.TexImage2D, "glTexImage2D", missing) &&
           Resolve(gl.TexParameteri, "glTexParameteri", missing) &&
           Resolve(gl.TexSubImage2D, "glTexSubImage2D", missing) &&
           Resolve(gl.Uniform1i, "glUniform1i", missing) &&
           Resolve(gl.Uniform1f, "glUniform1f", missing) &&
           Resolve(gl.Uniform3fv, "glUniform3fv", missing) &&
           Resolve(gl.Uniform4f, "glUniform4f", missing) &&
           Resolve(gl.UniformMatrix3fv, "glUniformMatrix3fv", missing) &&
           Resolve(gl.UseProgram, "glUseProgram", missing) &&
           Resolve(gl.Viewport, "glViewport", missing);
}

}
