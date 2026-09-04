/*
 * The OpenGL entry points the console renderer needs, and nothing else.
 *
 * libnack no longer exposes a proc-address getter, because the library owns
 * the renderer now; this is a private loader for the roughly forty functions
 * that renderer calls. It is not a general purpose GL loader and is not meant
 * to grow into one.
 */
#ifndef NACK_GL_H_INCLUDED
#define NACK_GL_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define NACK_GLAPI __stdcall
#else
#  define NACK_GLAPI
#endif

typedef unsigned int  nack_glenum;
typedef unsigned int  nack_gluint;
typedef int           nack_glint;
typedef int           nack_glsizei;
typedef unsigned int  nack_glbitfield;
typedef float         nack_glfloat;
typedef unsigned char nack_glboolean;
typedef char          nack_glchar;
typedef ptrdiff_t     nack_glsizeiptr;
typedef ptrdiff_t     nack_glintptr;

#define GL_FALSE            0
#define GL_TRUE             1
#define GL_TRIANGLES        0x0004
#define GL_FLOAT            0x1406
#define GL_UNSIGNED_BYTE    0x1401
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_ARRAY_BUFFER     0x8892
#define GL_STREAM_DRAW      0x88E0
#define GL_DYNAMIC_DRAW     0x88E8
#define GL_STATIC_DRAW      0x88E4
#define GL_VERTEX_SHADER    0x8B31
#define GL_FRAGMENT_SHADER  0x8B30
#define GL_COMPILE_STATUS   0x8B81
#define GL_LINK_STATUS      0x8B82
#define GL_TEXTURE_2D       0x0DE1
#define GL_TEXTURE0         0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S   0x2802
#define GL_TEXTURE_WRAP_T   0x2803
#define GL_NEAREST          0x2600
#define GL_LINEAR           0x2601
#define GL_CLAMP_TO_EDGE    0x812F
#define GL_RGBA             0x1908
#define GL_RGBA8            0x8058
#define GL_PACK_ALIGNMENT   0x0D05
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_BLEND            0x0BE2
#define GL_SRC_ALPHA        0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE              1
#define GL_ZERO             0
#define GL_SCISSOR_TEST     0x0C11
#define GL_VERSION          0x1F02
#define GL_RENDERER         0x1F01
#define GL_MAX_TEXTURE_SIZE 0x0D33

/* Each entry point becomes a function pointer resolved once at init. */
#define NACK_GL_FUNCTIONS(X)                                                     \
    X(void, Clear, (nack_glbitfield))                                            \
    X(void, ClearColor, (nack_glfloat, nack_glfloat, nack_glfloat, nack_glfloat))\
    X(void, Viewport, (nack_glint, nack_glint, nack_glsizei, nack_glsizei))      \
    X(void, Scissor, (nack_glint, nack_glint, nack_glsizei, nack_glsizei))       \
    X(void, Enable, (nack_glenum))                                               \
    X(void, Disable, (nack_glenum))                                              \
    X(void, BlendFunc, (nack_glenum, nack_glenum))                               \
    X(void, PixelStorei, (nack_glenum, nack_glint))                              \
    X(void, GetIntegerv, (nack_glenum, nack_glint *))                            \
    X(const unsigned char *, GetString, (nack_glenum))                           \
    X(nack_gluint, CreateShader, (nack_glenum))                                  \
    X(void, ShaderSource, (nack_gluint, nack_glsizei, const nack_glchar *const *,\
                           const nack_glint *))                                  \
    X(void, CompileShader, (nack_gluint))                                        \
    X(void, GetShaderiv, (nack_gluint, nack_glenum, nack_glint *))               \
    X(void, GetShaderInfoLog, (nack_gluint, nack_glsizei, nack_glsizei *,        \
                               nack_glchar *))                                   \
    X(void, DeleteShader, (nack_gluint))                                         \
    X(nack_gluint, CreateProgram, (void))                                        \
    X(void, AttachShader, (nack_gluint, nack_gluint))                            \
    X(void, LinkProgram, (nack_gluint))                                          \
    X(void, GetProgramiv, (nack_gluint, nack_glenum, nack_glint *))              \
    X(void, GetProgramInfoLog, (nack_gluint, nack_glsizei, nack_glsizei *,       \
                                nack_glchar *))                                  \
    X(void, UseProgram, (nack_gluint))                                           \
    X(void, DeleteProgram, (nack_gluint))                                        \
    X(nack_glint, GetUniformLocation, (nack_gluint, const nack_glchar *))        \
    X(void, Uniform1i, (nack_glint, nack_glint))                                 \
    X(void, Uniform2f, (nack_glint, nack_glfloat, nack_glfloat))                 \
    X(void, GenVertexArrays, (nack_glsizei, nack_gluint *))                      \
    X(void, BindVertexArray, (nack_gluint))                                      \
    X(void, DeleteVertexArrays, (nack_glsizei, const nack_gluint *))             \
    X(void, GenBuffers, (nack_glsizei, nack_gluint *))                           \
    X(void, BindBuffer, (nack_glenum, nack_gluint))                              \
    X(void, BufferData, (nack_glenum, nack_glsizeiptr, const void *, nack_glenum))\
    X(void, DeleteBuffers, (nack_glsizei, const nack_gluint *))                  \
    X(void, VertexAttribPointer, (nack_gluint, nack_glint, nack_glenum,          \
                                  nack_glboolean, nack_glsizei, const void *))   \
    X(void, EnableVertexAttribArray, (nack_gluint))                              \
    X(void, DrawArrays, (nack_glenum, nack_glint, nack_glsizei))                 \
    X(void, GenTextures, (nack_glsizei, nack_gluint *))                          \
    X(void, BindTexture, (nack_glenum, nack_gluint))                             \
    X(void, DeleteTextures, (nack_glsizei, const nack_gluint *))                 \
    X(void, ActiveTexture, (nack_glenum))                                        \
    X(void, TexParameteri, (nack_glenum, nack_glenum, nack_glint))               \
    X(void, TexImage2D, (nack_glenum, nack_glint, nack_glint, nack_glsizei,      \
                         nack_glsizei, nack_glint, nack_glenum, nack_glenum,     \
                         const void *))                                          \
    X(void, ReadPixels, (nack_glint, nack_glint, nack_glsizei, nack_glsizei,     \
                         nack_glenum, nack_glenum, void *))

#define NACK_GL_DECLARE(ret, name, args) \
    typedef ret (NACK_GLAPI *nack_pfn_gl##name) args; \
    extern nack_pfn_gl##name nack_gl##name;
NACK_GL_FUNCTIONS(NACK_GL_DECLARE)
#undef NACK_GL_DECLARE

/* Spelled the usual way at the call sites. */
#define glClear                  nack_glClear
#define glClearColor             nack_glClearColor
#define glViewport               nack_glViewport
#define glScissor                nack_glScissor
#define glEnable                 nack_glEnable
#define glDisable                nack_glDisable
#define glBlendFunc              nack_glBlendFunc
#define glPixelStorei            nack_glPixelStorei
#define glGetIntegerv            nack_glGetIntegerv
#define glGetString              nack_glGetString
#define glCreateShader           nack_glCreateShader
#define glShaderSource           nack_glShaderSource
#define glCompileShader          nack_glCompileShader
#define glGetShaderiv            nack_glGetShaderiv
#define glGetShaderInfoLog       nack_glGetShaderInfoLog
#define glDeleteShader           nack_glDeleteShader
#define glCreateProgram          nack_glCreateProgram
#define glAttachShader           nack_glAttachShader
#define glLinkProgram            nack_glLinkProgram
#define glGetProgramiv           nack_glGetProgramiv
#define glGetProgramInfoLog      nack_glGetProgramInfoLog
#define glUseProgram             nack_glUseProgram
#define glDeleteProgram          nack_glDeleteProgram
#define glGetUniformLocation     nack_glGetUniformLocation
#define glUniform1i              nack_glUniform1i
#define glUniform2f              nack_glUniform2f
#define glGenVertexArrays        nack_glGenVertexArrays
#define glBindVertexArray        nack_glBindVertexArray
#define glDeleteVertexArrays     nack_glDeleteVertexArrays
#define glGenBuffers             nack_glGenBuffers
#define glBindBuffer             nack_glBindBuffer
#define glBufferData             nack_glBufferData
#define glDeleteBuffers          nack_glDeleteBuffers
#define glVertexAttribPointer    nack_glVertexAttribPointer
#define glEnableVertexAttribArray nack_glEnableVertexAttribArray
#define glDrawArrays             nack_glDrawArrays
#define glGenTextures            nack_glGenTextures
#define glBindTexture            nack_glBindTexture
#define glDeleteTextures         nack_glDeleteTextures
#define glActiveTexture          nack_glActiveTexture
#define glTexParameteri          nack_glTexParameteri
#define glTexImage2D             nack_glTexImage2D
#define glReadPixels             nack_glReadPixels

namespace nack { namespace detail {
/* Resolves everything above; a context must be current. */
bool gl_load(const char **error);
} }   /* namespace nack::detail */

#endif /* NACK_GL_H_INCLUDED */
