/*
 * Picks a renderer and forwards to it.
 *
 * On macOS both Metal and OpenGL are available, and Metal is tried first
 * because Apple deprecated OpenGL and capped it at 4.1. If Metal cannot start
 * for any reason the OpenGL path takes over, which matters more than usual
 * here: the Metal backend shares no code with the platforms this library is
 * actually exercised on, so it is the one most likely to be wrong. A user who
 * hits that can also force the choice with NACK_RENDERER rather than having to
 * rebuild.
 */
#include "nack_gfx.h"
#include "nack_console_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nack { namespace detail {

static nack_gfx_backend *gfx;

/*
 * A backend that always fails to start, so the fallback below is covered by
 * the test suite on machines that only have one real renderer - which is every
 * machine this library is developed on. It is only ever reachable through
 * NACK_RENDERER=test-fail.
 *
 * As a table this was nine trailing NULLs, and safe only because it never
 * starts: had it ever been selected, the first draw would have called through
 * a null pointer. It has to answer every operation now, so that cannot happen
 * however it comes to be chosen.
 */
namespace {

class failing_backend final : public nack_gfx_backend {
public:
    const char *name() const override { return "test-fail"; }

    bool init(nack_window *window) override
    {
        (void)window;
        return console_state.set_error("this renderer fails on purpose");
    }
    void shutdown() override {}

    nack_texture *texture_create(const uint8_t *, int, int) override
    {
        console_state.set_error("the test-fail renderer draws nothing");
        return nullptr;
    }
    void texture_destroy(nack_texture *) override {}
    void begin_frame(nack_color, int, int, int, int, int, int) override {}
    void draw(const float *, size_t, int, nack_texture *) override {}
    void end_frame() override {}
    void resize(int, int) override {}
    void set_vsync(bool) override {}
};

failing_backend gfx_fail_backend;

}   /* namespace */

bool gfx_init(nack_window *window)
{
    nack_gfx_backend *candidates[3];
    const char *preferred = getenv("NACK_RENDERER");
    char reason[256];
    size_t count = 0, i, j;

    reason[0] = '\0';

    if (preferred && !*preferred)
        preferred = nullptr;

    if (preferred && strcmp(preferred, "test-fail") == 0)
        candidates[count++] = &gfx_fail_backend;
#if defined(__APPLE__) && !defined(NACK_MACOS_OPENGL_ONLY)
    candidates[count++] = gfx_backend_metal();
#endif
    candidates[count++] = gfx_backend_gl();

    /* A named renderer goes first; the rest stay as fallbacks behind it. */
    if (preferred) {
        for (i = 0; i < count; ++i) {
            nack_gfx_backend *chosen = candidates[i];

            if (!chosen || strcmp(chosen->name(), preferred) != 0)
                continue;
            for (j = i; j > 0; --j)
                candidates[j] = candidates[j - 1];
            candidates[0] = chosen;
            break;
        }
        if (i == count)
            nack_log("nack: NACK_RENDERER=%s names no renderer this build "
                      "has; ignoring it", preferred);
    }

    for (i = 0; i < count; ++i) {
        const char *why;

        if (!candidates[i])
            continue;
        gfx = candidates[i];
        if (gfx->init(window)) {
            /*
             * A renderer we then fell back from left its complaint behind;
             * the caller only cares that one of them worked.
             */
            console_state.clear_error();
            nack_log("nack: rendering with %s", gfx->name());
            return true;
        }

        why = console_state.last_error();
        nack_log("nack: the %s renderer is unavailable: %s", gfx->name(),
                  why ? why : "no reason given");
        /*
         * Keep the last renderer's complaint. If every candidate fails, that
         * is the only account of why anywhere, and "no renderer could be
         * started" on its own tells a user nothing they can act on.
         */
        snprintf(reason, sizeof reason, "%s: %s", gfx->name(),
                 why ? why : "no reason given");
        /*
         * Every backend's shutdown copes with a half-built state, and has to
         * put the window back as it found it so the next one can have it.
         */
        gfx->shutdown();
        gfx = nullptr;
    }

    return console_state.set_error("no renderer could be started (%s)",
                       reason[0] ? reason : "none were compiled in");
}

void gfx_shutdown(void)
{
    if (gfx)
        gfx->shutdown();
    gfx = nullptr;
}

const char *gfx_name(void)
{
    return gfx ? gfx->name() : "none";
}

static int gfx_failed_textures;

void debug_fail_next_textures(int count)
{
    gfx_failed_textures = count;
}

nack_texture *gfx_texture_create(const uint8_t *rgba, int width,
                                              int height)
{
    if (gfx_failed_textures > 0) {
        --gfx_failed_textures;
        console_state.set_error("texture creation failed on purpose");
        return nullptr;
    }
    if (!gfx) {
        console_state.set_error("no renderer is active");
        return nullptr;
    }
    return gfx->texture_create(rgba, width, height);
}

void gfx_texture_destroy(nack_texture *texture)
{
    if (gfx && texture)
        gfx->texture_destroy(texture);
}

void gfx_begin_frame(nack_color clear, int fb_width, int fb_height,
                           int viewport_x, int viewport_y, int viewport_w,
                           int viewport_h)
{
    if (gfx)
        gfx->begin_frame(clear, fb_width, fb_height, viewport_x,
                               viewport_y, viewport_w, viewport_h);
}

void gfx_draw(const float *vertices, size_t vertex_count, int mode,
                    nack_texture *texture)
{
    if (gfx)
        gfx->draw(vertices, vertex_count, mode, texture);
}

void gfx_end_frame(void)
{
    if (gfx)
        gfx->end_frame();
}

void gfx_resize(int fb_width, int fb_height)
{
    if (gfx)
        gfx->resize(fb_width, fb_height);
}

void gfx_set_vsync(bool vsync)
{
    if (gfx)
        gfx->set_vsync(vsync);
}

void gfx_set_capture(bool capture)
{
    if (gfx)
        gfx->set_capture(capture);
}

bool gfx_read_pixel(int x, int y, uint8_t rgba[4])
{
    return gfx ? gfx->read_pixel(x, y, rgba) : false;
}

} }   /* namespace nack::detail */
