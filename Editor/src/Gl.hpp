#pragma once

// The few OpenGL entry points the editor uses directly (preview framebuffer and
// textures). ImGui's OpenGL3 backend has its own loader for everything else.

#include <cstdint>

namespace gl
{
    using GLenum     = unsigned int;
    using GLuint     = unsigned int;
    using GLint      = int;
    using GLsizei    = int;
    using GLbitfield = unsigned int;
    using GLfloat    = float;

    constexpr GLenum TEXTURE_2D           = 0x0DE1;
    constexpr GLenum RGBA                 = 0x1908;
    constexpr GLenum RGBA8                = 0x8058;
    constexpr GLenum UNSIGNED_BYTE        = 0x1401;
    constexpr GLenum TEXTURE_MIN_FILTER   = 0x2801;
    constexpr GLenum TEXTURE_MAG_FILTER   = 0x2800;
    constexpr GLenum NEAREST              = 0x2600;
    constexpr GLenum LINEAR               = 0x2601;
    constexpr GLenum LINEAR_MIPMAP_LINEAR = 0x2703;
    constexpr GLenum TEXTURE_WRAP_S       = 0x2802;
    constexpr GLenum TEXTURE_WRAP_T       = 0x2803;
    constexpr GLenum CLAMP_TO_EDGE        = 0x812F;
    constexpr GLenum FRAMEBUFFER          = 0x8D40;
    constexpr GLenum FRAMEBUFFER_BINDING  = 0x8CA6;
    constexpr GLenum COLOR_ATTACHMENT0    = 0x8CE0;
    constexpr GLenum FRAMEBUFFER_COMPLETE = 0x8CD5;
    constexpr GLenum COLOR_BUFFER_BIT     = 0x4000;
    constexpr GLenum PACK_ALIGNMENT       = 0x0D05;
    constexpr GLenum UNPACK_ALIGNMENT     = 0x0CF5;
    constexpr GLenum VIEWPORT             = 0x0BA2;
    constexpr GLenum TEXTURE_BINDING_2D   = 0x8069;

#if defined(_WIN32)
#define HS_GLAPI __stdcall
#else
#define HS_GLAPI
#endif

#define HS_GL_FUNCTIONS(X) \
    X(void,   GenTextures,            (GLsizei n, GLuint* textures)) \
    X(void,   DeleteTextures,         (GLsizei n, const GLuint* textures)) \
    X(void,   BindTexture,            (GLenum target, GLuint texture)) \
    X(void,   TexImage2D,             (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels)) \
    X(void,   TexParameteri,          (GLenum target, GLenum pname, GLint param)) \
    X(void,   GenerateMipmap,         (GLenum target)) \
    X(void,   GenFramebuffers,        (GLsizei n, GLuint* framebuffers)) \
    X(void,   DeleteFramebuffers,     (GLsizei n, const GLuint* framebuffers)) \
    X(void,   BindFramebuffer,        (GLenum target, GLuint framebuffer)) \
    X(void,   FramebufferTexture2D,   (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(GLenum, CheckFramebufferStatus, (GLenum target)) \
    X(void,   Viewport,               (GLint x, GLint y, GLsizei width, GLsizei height)) \
    X(void,   ClearColor,             (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X(void,   Clear,                  (GLbitfield mask)) \
    X(void,   ReadPixels,             (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels)) \
    X(void,   PixelStorei,            (GLenum pname, GLint param)) \
    X(void,   GetIntegerv,            (GLenum pname, GLint* data))

#define HS_GL_DECLARE(ret, name, args) using PFN_##name = ret (HS_GLAPI*) args; extern PFN_##name name;
    HS_GL_FUNCTIONS(HS_GL_DECLARE)
#undef HS_GL_DECLARE

    // Call with a current GL context. Returns false if a function is missing.
    bool Load();
}
