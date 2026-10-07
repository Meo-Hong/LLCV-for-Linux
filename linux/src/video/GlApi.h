#pragma once

#include <SDL3/SDL_opengl.h>

#include <string>

namespace llcv::video {

struct GlApi {
    void (*ActiveTexture)(GLenum);
    void (*AttachShader)(GLuint, GLuint);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*BindTexture)(GLenum, GLuint);
    void (*BindVertexArray)(GLuint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    void (*Clear)(GLbitfield);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*CompileShader)(GLuint);
    GLuint (*CreateProgram)();
    GLuint (*CreateShader)(GLenum);
    void (*DeleteFramebuffers)(GLsizei, const GLuint*);
    void (*DeleteProgram)(GLuint);
    void (*DeleteShader)(GLuint);
    void (*DeleteTextures)(GLsizei, const GLuint*);
    void (*DeleteVertexArrays)(GLsizei, const GLuint*);
    void (*Disable)(GLenum);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*GenTextures)(GLsizei, GLuint*);
    void (*GenVertexArrays)(GLsizei, GLuint*);
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (*GetProgramiv)(GLuint, GLenum, GLint*);
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (*GetShaderiv)(GLuint, GLenum, GLint*);
    const GLubyte* (*GetString)(GLenum);
    GLint (*GetUniformLocation)(GLuint, const GLchar*);
    void (*LinkProgram)(GLuint);
    void (*PixelStorei)(GLenum, GLint);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
    void (*Uniform1i)(GLint, GLint);
    void (*Uniform1f)(GLint, GLfloat);
    void (*Uniform3fv)(GLint, GLsizei, const GLfloat*);
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*UniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void (*UseProgram)(GLuint);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
};

extern GlApi gl;

bool LoadGlApi(std::string& missing);

}
