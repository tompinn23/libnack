/*
 * A minimal OpenGL 3.3 core loader for the examples.
 *
 * libnack deliberately ships no loader of its own: nack_gl_get_proc_address is
 * a plain getter, so a real project points glad, epoxy or glew at it. This
 * file exists only so the examples build with no external dependency, and it
 * shows the shape of that wiring:
 *
 *     gladLoadGLLoader((GLADloadproc)nack_gl_get_proc_address);
 */
#ifndef NACK_EXAMPLE_GL_LOADER_H
#define NACK_EXAMPLE_GL_LOADER_H

#include "nack/nack.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef void GLvoid;

#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_TRIANGLES          0x0004
#define GL_FLOAT              0x1406
#define GL_UNSIGNED_BYTE      0x1401
#define GL_FALSE              0
#define GL_TRUE               1
#define GL_ARRAY_BUFFER       0x8892
#define GL_STATIC_DRAW        0x88E4
#define GL_DYNAMIC_DRAW       0x88E8
#define GL_VERTEX_SHADER      0x8B31
#define GL_FRAGMENT_SHADER    0x8B30
#define GL_COMPILE_STATUS     0x8B81
#define GL_LINK_STATUS        0x8B82
#define GL_TEXTURE_2D         0x0DE1
#define GL_TEXTURE0           0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_NEAREST            0x2600
#define GL_CLAMP_TO_EDGE      0x812F
#define GL_RED                0x1903
#define GL_R8                 0x8229
#define GL_UNPACK_ALIGNMENT   0x0CF5
#define GL_BLEND              0x0BE2
#define GL_SRC_ALPHA          0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_VERSION            0x1F02
#define GL_RENDERER           0x1F01

#define NACK_GL_FUNCTIONS(X)                                                          \
    X(void,   glClear,              (GLbitfield))                                     \
    X(void,   glClearColor,         (GLfloat, GLfloat, GLfloat, GLfloat))             \
    X(void,   glViewport,           (GLint, GLint, GLsizei, GLsizei))                 \
    X(const GLubyte *, glGetString, (GLenum))                                         \
    X(void,   glEnable,             (GLenum))                                         \
    X(void,   glBlendFunc,          (GLenum, GLenum))                                 \
    X(void,   glPixelStorei,        (GLenum, GLint))                                  \
    X(GLuint, glCreateShader,       (GLenum))                                         \
    X(void,   glShaderSource,       (GLuint, GLsizei, const GLchar *const *,          \
                                     const GLint *))                                  \
    X(void,   glCompileShader,      (GLuint))                                         \
    X(void,   glGetShaderiv,        (GLuint, GLenum, GLint *))                        \
    X(void,   glGetShaderInfoLog,   (GLuint, GLsizei, GLsizei *, GLchar *))           \
    X(void,   glDeleteShader,       (GLuint))                                         \
    X(GLuint, glCreateProgram,      (void))                                           \
    X(void,   glAttachShader,       (GLuint, GLuint))                                 \
    X(void,   glLinkProgram,        (GLuint))                                         \
    X(void,   glGetProgramiv,       (GLuint, GLenum, GLint *))                        \
    X(void,   glGetProgramInfoLog,  (GLuint, GLsizei, GLsizei *, GLchar *))           \
    X(void,   glUseProgram,         (GLuint))                                         \
    X(void,   glDeleteProgram,      (GLuint))                                         \
    X(GLint,  glGetUniformLocation, (GLuint, const GLchar *))                         \
    X(void,   glUniform1i,          (GLint, GLint))                                   \
    X(void,   glUniform2f,          (GLint, GLfloat, GLfloat))                        \
    X(void,   glGenVertexArrays,    (GLsizei, GLuint *))                              \
    X(void,   glBindVertexArray,    (GLuint))                                         \
    X(void,   glDeleteVertexArrays, (GLsizei, const GLuint *))                        \
    X(void,   glGenBuffers,         (GLsizei, GLuint *))                              \
    X(void,   glBindBuffer,         (GLenum, GLuint))                                 \
    X(void,   glBufferData,         (GLenum, GLsizeiptr, const void *, GLenum))       \
    X(void,   glDeleteBuffers,      (GLsizei, const GLuint *))                        \
    X(void,   glVertexAttribPointer,(GLuint, GLint, GLenum, GLboolean, GLsizei,       \
                                     const void *))                                   \
    X(void,   glEnableVertexAttribArray, (GLuint))                                    \
    X(void,   glDrawArrays,         (GLenum, GLint, GLsizei))                         \
    X(void,   glGenTextures,        (GLsizei, GLuint *))                              \
    X(void,   glBindTexture,        (GLenum, GLuint))                                 \
    X(void,   glDeleteTextures,     (GLsizei, const GLuint *))                        \
    X(void,   glActiveTexture,      (GLenum))                                         \
    X(void,   glTexParameteri,      (GLenum, GLenum, GLint))                          \
    X(void,   glTexImage2D,         (GLenum, GLint, GLint, GLsizei, GLsizei, GLint,   \
                                     GLenum, GLenum, const void *))

/* One function pointer per entry point, resolved once - the same shape a
 * generated loader produces. */
#define NACK_GL_DECLARE(ret, name, args) \
    typedef ret (*nack_pfn_##name) args; \
    static nack_pfn_##name name;
NACK_GL_FUNCTIONS(NACK_GL_DECLARE)
#undef NACK_GL_DECLARE

static bool nack_example_load_gl(void)
{
    bool ok = true;
#define NACK_GL_RESOLVE(ret, name, args)                            \
    name = (nack_pfn_##name)nack_gl_get_proc_address(#name);        \
    if (!name) {                                                    \
        fprintf(stderr, "could not resolve %s\n", #name);           \
        ok = false;                                                 \
    }
    NACK_GL_FUNCTIONS(NACK_GL_RESOLVE)
#undef NACK_GL_RESOLVE
    return ok;
}

static GLuint nack_example_compile(GLenum stage, const char *source)
{
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        fprintf(stderr, "shader compilation failed:\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint nack_example_program(const char *vertex_source,
                                   const char *fragment_source)
{
    GLuint vertex_shader = nack_example_compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment_shader = nack_example_compile(GL_FRAGMENT_SHADER,
                                                  fragment_source);
    if (!vertex_shader || !fragment_shader)
        return 0;

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof log, NULL, log);
        fprintf(stderr, "program link failed:\n%s\n", log);
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return program;
}

#endif /* NACK_EXAMPLE_GL_LOADER_H */
