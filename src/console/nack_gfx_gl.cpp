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
struct nack__gl_texture {
    nack_gluint id;
};

struct nack_gl_backend {
    struct nack_window *window;
    struct nack_gl_context *context;
    nack_gluint program;
    nack_gluint vao, vbo;
    nack_glint uniform_viewport, uniform_atlas, uniform_mode;
    int viewport_w, viewport_h;
    int fb_width, fb_height;

    /* The last frame, kept only when capture is on. See nack__glr_end_frame. */
    bool capture;
    std::vector<uint8_t> frame;
    int frame_width, frame_height;
};

static struct nack_gl_backend nack__gl;

static const char *nack__vertex_source =
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

static const char *nack__fragment_source =
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

static nack_gluint nack__compile(nack_glenum stage, const char *source)
{
    nack_gluint shader = glCreateShader(stage);
    nack_glint status = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        nack__c.set_error("console shader failed to compile: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool nack__glr_init(struct nack_window *window)
{
    struct nack__gl_desc desc;
    const char *missing = NULL;
    nack_gluint vertex, fragment;
    nack_glint status = 0;

    nack__gl = nack_gl_backend{};
    nack__gl.window = window;

    desc.major = 3;
    desc.minor = 3;
    desc.profile = NACK__GL_PROFILE_CORE;

    nack__gl.context = nack_gl_context::create(window, &desc);
    if (!nack__gl.context) {
        const char *message = NULL;
        state.last_error(&message);
        return nack__c.set_error("cannot create an OpenGL 3.3 context: %s",
                           message ? message : "unknown");
    }
    state.gl_make_current(window, nack__gl.context);

    if (!nack__gl_load(&missing))
        return nack__c.set_error("this OpenGL driver is missing %s",
                           missing ? missing : "required entry points");

    vertex = nack__compile(GL_VERTEX_SHADER, nack__vertex_source);
    if (!vertex)
        return false;
    fragment = nack__compile(GL_FRAGMENT_SHADER, nack__fragment_source);
    if (!fragment) {
        glDeleteShader(vertex);
        return false;
    }

    nack__gl.program = glCreateProgram();
    glAttachShader(nack__gl.program, vertex);
    glAttachShader(nack__gl.program, fragment);
    glLinkProgram(nack__gl.program);
    glGetProgramiv(nack__gl.program, GL_LINK_STATUS, &status);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!status) {
        char log[1024];
        glGetProgramInfoLog(nack__gl.program, sizeof log, NULL, log);
        glDeleteProgram(nack__gl.program);
        nack__gl.program = 0;
        return nack__c.set_error("console shader failed to link: %s", log);
    }

    nack__gl.uniform_viewport = glGetUniformLocation(nack__gl.program, "u_viewport");
    nack__gl.uniform_atlas = glGetUniformLocation(nack__gl.program, "u_atlas");
    nack__gl.uniform_mode = glGetUniformLocation(nack__gl.program, "u_mode");

    glGenVertexArrays(1, &nack__gl.vao);
    glBindVertexArray(nack__gl.vao);
    glGenBuffers(1, &nack__gl.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, nack__gl.vbo);

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

static void nack__glr_shutdown(void)
{
    if (nack__gl.vbo) glDeleteBuffers(1, &nack__gl.vbo);
    if (nack__gl.vao) glDeleteVertexArrays(1, &nack__gl.vao);
    if (nack__gl.program) glDeleteProgram(nack__gl.program);
    if (nack__gl.context) nack_gl_context::destroy(nack__gl.context);
    nack__gl = nack_gl_backend{};
}

static struct nack_texture *nack__glr_texture_create(const uint8_t *rgba,
                                                     int width, int height)
{
    struct nack__gl_texture *texture = new nack__gl_texture{};

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
    return (struct nack_texture *)texture;
}

static void nack__glr_texture_destroy(struct nack_texture *handle)
{
    struct nack__gl_texture *texture = (struct nack__gl_texture *)handle;

    if (!texture)
        return;
    if (texture->id)
        glDeleteTextures(1, &texture->id);
    delete texture;
}

static void nack__glr_begin_frame(struct nack_color clear, int fb_width,
                                  int fb_height, int viewport_x,
                                  int viewport_y, int viewport_w,
                                  int viewport_h)
{
    nack__gl.viewport_w = viewport_w;
    nack__gl.viewport_h = viewport_h;
    nack__gl.fb_width = fb_width;
    nack__gl.fb_height = fb_height;

    glViewport(0, 0, fb_width, fb_height);
    glClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* GL's origin is bottom left, so the console's y is measured from there. */
    glViewport(viewport_x, fb_height - viewport_y - viewport_h,
               viewport_w, viewport_h);

    glUseProgram(nack__gl.program);
    glUniform2f(nack__gl.uniform_viewport, (float)viewport_w, (float)viewport_h);
    glUniform1i(nack__gl.uniform_atlas, 0);
    glBindVertexArray(nack__gl.vao);
    glBindBuffer(GL_ARRAY_BUFFER, nack__gl.vbo);
    glActiveTexture(GL_TEXTURE0);
}

static void nack__glr_draw(const float *vertices, size_t vertex_count,
                           int mode, struct nack_texture *handle)
{
    struct nack__gl_texture *texture = (struct nack__gl_texture *)handle;

    if (vertex_count == 0)
        return;

    glUniform1i(nack__gl.uniform_mode, mode);
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
static void nack__glr_capture_frame(void)
{
    size_t bytes;

    if (nack__gl.fb_width <= 0 || nack__gl.fb_height <= 0)
        return;

    if (nack__gl.frame_width != nack__gl.fb_width ||
        nack__gl.frame_height != nack__gl.fb_height) {
        nack__gl.frame.clear();
        nack__gl.frame_width = 0;
        nack__gl.frame_height = 0;
    }
    if (nack__gl.frame.empty()) {
        bytes = (size_t)nack__gl.fb_width * (size_t)nack__gl.fb_height * 4;
        try {
            nack__gl.frame.resize(bytes);
        } catch (const std::exception &) {
            return;
        }
        nack__gl.frame_width = nack__gl.fb_width;
        nack__gl.frame_height = nack__gl.fb_height;
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, nack__gl.frame_width, nack__gl.frame_height, GL_RGBA,
                 GL_UNSIGNED_BYTE, nack__gl.frame.data());
}

static void nack__glr_end_frame(void)
{
    if (nack__gl.capture)
        nack__glr_capture_frame();
    state.gl_swap_buffers(nack__gl.window);
}

static void nack__glr_resize(int fb_width, int fb_height)
{
    (void)fb_width;
    nack__gl.fb_height = fb_height;
}

static void nack__glr_set_vsync(bool vsync)
{
    state.gl_set_swap_interval(vsync ? 1 : 0);
}

static void nack__glr_set_capture(bool capture)
{
    nack__gl.capture = capture;
    if (!capture) {
        nack__gl.frame.clear();
        nack__gl.frame_width = 0;
        nack__gl.frame_height = 0;
    }
}

static bool nack__glr_read_pixel(int x, int y, uint8_t rgba[4])
{
    const uint8_t *pixel;
    int row;

    if (nack__gl.frame.empty())
        return false;
    if (x < 0 || y < 0 || x >= nack__gl.frame_width ||
        y >= nack__gl.frame_height)
        return false;

    /* The capture is in GL's bottom-left order; callers count from the top. */
    row = nack__gl.frame_height - 1 - y;
    pixel = nack__gl.frame.data() + ((size_t)row * nack__gl.frame_width + x) * 4;
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

    bool init(struct nack_window *window) override
    {
        return nack__glr_init(window);
    }
    void shutdown() override { nack__glr_shutdown(); }

    struct nack_texture *texture_create(const uint8_t *rgba, int width,
                                        int height) override
    {
        return nack__glr_texture_create(rgba, width, height);
    }
    void texture_destroy(struct nack_texture *texture) override
    {
        nack__glr_texture_destroy(texture);
    }

    void begin_frame(struct nack_color clear, int fb_width, int fb_height,
                     int viewport_x, int viewport_y, int viewport_w,
                     int viewport_h) override
    {
        nack__glr_begin_frame(clear, fb_width, fb_height, viewport_x,
                              viewport_y, viewport_w, viewport_h);
    }
    void draw(const float *vertices, size_t vertex_count, int mode,
              struct nack_texture *texture) override
    {
        nack__glr_draw(vertices, vertex_count, mode, texture);
    }
    void end_frame() override { nack__glr_end_frame(); }

    void resize(int fb_width, int fb_height) override
    {
        nack__glr_resize(fb_width, fb_height);
    }
    void set_vsync(bool vsync) override { nack__glr_set_vsync(vsync); }

    void set_capture(bool capture) override { nack__glr_set_capture(capture); }
    bool read_pixel(int x, int y, uint8_t rgba[4]) override
    {
        return nack__glr_read_pixel(x, y, rgba);
    }
};

gl_backend nack__gl_backend_instance;

}   /* namespace */

nack_gfx_backend *nack__gfx_backend_gl(void)
{
    return &nack__gl_backend_instance;
}
