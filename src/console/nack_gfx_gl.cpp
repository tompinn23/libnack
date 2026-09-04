/*
 * The OpenGL 3.3 implementation of the console's graphics backend.
 *
 * One quad per cell, uploaded per draw. A console is a few thousand cells, so
 * rebuilding the buffer outright is cheaper than tracking which cells changed.
 */
#include "nack_gfx.h"
#include "nack_gl.h"
#include "nack_console_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

/*
 * A texture is opaque to everything above, and on macOS both backends are
 * linked together, so each keeps its own type rather than two definitions of
 * one tag. Only the backend that made a texture ever looks inside it.
 */
namespace nack { namespace detail {

struct gl_texture {
    nack_gluint id;
};

struct nack_gl_backend {
    nack_window *window;
    nack_gl_context *context;
    nack_gluint program;
    nack_gluint vao, vbo;
    nack_glint uniform_viewport, uniform_atlas, uniform_mode;
    int viewport_w, viewport_h;
    int fb_width, fb_height;

    /* The last frame, kept only when capture is on. See glr_end_frame. */
    bool capture;
    std::vector<uint8_t> frame;
    int frame_width, frame_height;
};

static nack_gl_backend gl;

static const char *vertex_source =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_position;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_fg;\n"
    "layout(location = 3) in vec4 a_bg;\n"
    "uniform vec2 u_viewport;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_fg;\n"
    "out vec4 v_bg;\n"
    "void main() {\n"
    "    vec2 ndc = (a_position / u_viewport) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_fg = a_fg;\n"
    "    v_bg = a_bg;\n"
    "}\n";

static const char *fragment_source =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_fg;\n"
    "in vec4 v_bg;\n"
    "uniform sampler2D u_atlas;\n"
    "uniform int u_mode;\n"
    "out vec4 frag_colour;\n"
    "void main() {\n"
    "    if (u_mode == 0) {\n"
    "        frag_colour = v_bg;\n"
    "        return;\n"
    "    }\n"
    /* A font atlas is white with coverage in alpha, so the multiply tints it.
     * A colour tileset carries its own rgb and the tint is usually white. */
    "    vec4 texel = texture(u_atlas, v_uv);\n"
    "    frag_colour = vec4(v_fg.rgb * texel.rgb, v_fg.a * texel.a);\n"
    "    if (frag_colour.a <= 0.0) discard;\n"
    "}\n";

static nack_gluint compile(nack_glenum stage, const char *source)
{
    nack_gluint shader = glCreateShader(stage);
    nack_glint status = 0;

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        console_state.set_error("console shader failed to compile: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool glr_init(nack_window *window)
{
    gl_desc desc;
    const char *missing = nullptr;
    nack_gluint vertex, fragment;
    nack_glint status = 0;

    gl = nack_gl_backend{};
    gl.window = window;

    desc.major = 3;
    desc.minor = 3;
    desc.profile = NACK__GL_PROFILE_CORE;

    gl.context = nack_gl_context::create(window, &desc);
    if (!gl.context) {
        const char *message = nullptr;
        state.last_error(&message);
        return console_state.set_error("cannot create an OpenGL 3.3 context: %s",
                           message ? message : "unknown");
    }
    state.gl_make_current(window, gl.context);

    if (!gl_load(&missing))
        return console_state.set_error("this OpenGL driver is missing %s",
                           missing ? missing : "required entry points");

    vertex = compile(GL_VERTEX_SHADER, vertex_source);
    if (!vertex)
        return false;
    fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
    if (!fragment) {
        glDeleteShader(vertex);
        return false;
    }

    gl.program = glCreateProgram();
    glAttachShader(gl.program, vertex);
    glAttachShader(gl.program, fragment);
    glLinkProgram(gl.program);
    glGetProgramiv(gl.program, GL_LINK_STATUS, &status);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!status) {
        char log[1024];
        glGetProgramInfoLog(gl.program, sizeof log, nullptr, log);
        glDeleteProgram(gl.program);
        gl.program = 0;
        return console_state.set_error("console shader failed to link: %s", log);
    }

    gl.uniform_viewport = glGetUniformLocation(gl.program, "u_viewport");
    gl.uniform_atlas = glGetUniformLocation(gl.program, "u_atlas");
    gl.uniform_mode = glGetUniformLocation(gl.program, "u_mode");

    glGenVertexArrays(1, &gl.vao);
    glBindVertexArray(gl.vao);
    glGenBuffers(1, &gl.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gl.vbo);

    {
        const nack_glsizei stride =
            NACK_FLOATS_PER_VERTEX * (nack_glsizei)sizeof(float);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(0 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(4 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(8 * sizeof(float)));
        glEnableVertexAttribArray(3);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

static void glr_shutdown(void)
{
    if (gl.vbo) glDeleteBuffers(1, &gl.vbo);
    if (gl.vao) glDeleteVertexArrays(1, &gl.vao);
    if (gl.program) glDeleteProgram(gl.program);
    if (gl.context) nack_gl_context::destroy(gl.context);
    gl = nack_gl_backend{};
}

static nack_texture *glr_texture_create(const uint8_t *rgba,
                                                     int width, int height)
{
    gl_texture *texture = new gl_texture{};

    glGenTextures(1, &texture->id);
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    /* Nearest keeps pixel art crisp; a console never wants smoothing. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return (nack_texture *)texture;
}

static void glr_texture_destroy(nack_texture *handle)
{
    gl_texture *texture = (gl_texture *)handle;

    if (!texture)
        return;
    if (texture->id)
        glDeleteTextures(1, &texture->id);
    delete texture;
}

static void glr_begin_frame(nack_color clear, int fb_width,
                                  int fb_height, int viewport_x,
                                  int viewport_y, int viewport_w,
                                  int viewport_h)
{
    gl.viewport_w = viewport_w;
    gl.viewport_h = viewport_h;
    gl.fb_width = fb_width;
    gl.fb_height = fb_height;

    glViewport(0, 0, fb_width, fb_height);
    glClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* GL's origin is bottom left, so the console's y is measured from there. */
    glViewport(viewport_x, fb_height - viewport_y - viewport_h,
               viewport_w, viewport_h);

    glUseProgram(gl.program);
    glUniform2f(gl.uniform_viewport, (float)viewport_w, (float)viewport_h);
    glUniform1i(gl.uniform_atlas, 0);
    glBindVertexArray(gl.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gl.vbo);
    glActiveTexture(GL_TEXTURE0);
}

static void glr_draw(const float *vertices, size_t vertex_count,
                           int mode, nack_texture *handle)
{
    gl_texture *texture = (gl_texture *)handle;

    if (vertex_count == 0)
        return;

    glUniform1i(gl.uniform_mode, mode);
    if (texture)
        glBindTexture(GL_TEXTURE_2D, texture->id);

    glBufferData(GL_ARRAY_BUFFER,
                 (nack_glsizeiptr)(vertex_count * NACK_FLOATS_PER_VERTEX *
                                   sizeof(float)),
                 vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (nack_glsizei)vertex_count);
}

/*
 * Grabs the whole framebuffer before presenting it. This has to happen here:
 * once the buffers have been swapped the back buffer's contents are undefined
 * by the specification, and drivers differ on what they leave behind - Mesa's
 * software renderer on Windows leaves nothing, where the Linux drivers happen
 * to leave the frame intact. Reading after the swap therefore worked by luck
 * rather than by rule.
 */
static void glr_capture_frame(void)
{
    size_t bytes;

    if (gl.fb_width <= 0 || gl.fb_height <= 0)
        return;

    if (gl.frame_width != gl.fb_width ||
        gl.frame_height != gl.fb_height) {
        gl.frame.clear();
        gl.frame_width = 0;
        gl.frame_height = 0;
    }
    if (gl.frame.empty()) {
        bytes = (size_t)gl.fb_width * (size_t)gl.fb_height * 4;
        try {
            gl.frame.resize(bytes);
        } catch (const std::exception &) {
            return;
        }
        gl.frame_width = gl.fb_width;
        gl.frame_height = gl.fb_height;
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, gl.frame_width, gl.frame_height, GL_RGBA,
                 GL_UNSIGNED_BYTE, gl.frame.data());
}

static void glr_end_frame(void)
{
    if (gl.capture)
        glr_capture_frame();
    state.gl_swap_buffers(gl.window);
}

static void glr_resize(int fb_width, int fb_height)
{
    (void)fb_width;
    gl.fb_height = fb_height;
}

static void glr_set_vsync(bool vsync)
{
    state.gl_set_swap_interval(vsync ? 1 : 0);
}

static void glr_set_capture(bool capture)
{
    gl.capture = capture;
    if (!capture) {
        gl.frame.clear();
        gl.frame_width = 0;
        gl.frame_height = 0;
    }
}

static bool glr_read_pixel(int x, int y, uint8_t rgba[4])
{
    const uint8_t *pixel;
    int row;

    if (gl.frame.empty())
        return false;
    if (x < 0 || y < 0 || x >= gl.frame_width ||
        y >= gl.frame_height)
        return false;

    /* The capture is in GL's bottom-left order; callers count from the top. */
    row = gl.frame_height - 1 - y;
    pixel = gl.frame.data() + ((size_t)row * gl.frame_width + x) * 4;
    rgba[0] = pixel[0];
    rgba[1] = pixel[1];
    rgba[2] = pixel[2];
    rgba[3] = pixel[3];
    return true;
}

/*
 * The overrides are one line each and forward to the functions above, which
 * keep their own file-static state. Nothing about the OpenGL code changes;
 * what changes is that leaving one of them out is now a compile error.
 */
namespace {

class gl_backend final : public nack_gfx_backend {
public:
    const char *name() const override { return "opengl"; }

    bool init(nack_window *window) override
    {
        return glr_init(window);
    }
    void shutdown() override { glr_shutdown(); }

    nack_texture *texture_create(const uint8_t *rgba, int width,
                                        int height) override
    {
        return glr_texture_create(rgba, width, height);
    }
    void texture_destroy(nack_texture *texture) override
    {
        glr_texture_destroy(texture);
    }

    void begin_frame(nack_color clear, int fb_width, int fb_height,
                     int viewport_x, int viewport_y, int viewport_w,
                     int viewport_h) override
    {
        glr_begin_frame(clear, fb_width, fb_height, viewport_x,
                              viewport_y, viewport_w, viewport_h);
    }
    void draw(const float *vertices, size_t vertex_count, int mode,
              nack_texture *texture) override
    {
        glr_draw(vertices, vertex_count, mode, texture);
    }
    void end_frame() override { glr_end_frame(); }

    void resize(int fb_width, int fb_height) override
    {
        glr_resize(fb_width, fb_height);
    }
    void set_vsync(bool vsync) override { glr_set_vsync(vsync); }

    void set_capture(bool capture) override { glr_set_capture(capture); }
    bool read_pixel(int x, int y, uint8_t rgba[4]) override
    {
        return glr_read_pixel(x, y, rgba);
    }
};

gl_backend gl_backend_instance;

}   /* namespace */

nack_gfx_backend *gfx_backend_gl(void)
{
    return &gl_backend_instance;
}

} }   /* namespace nack::detail */
