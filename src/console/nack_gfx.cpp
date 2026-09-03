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

static const struct nack_gfx_backend *nack__gfx;

/*
 * A backend that always fails to start, so the fallback below is covered by
 * the test suite on machines that only have one real renderer - which is every
 * machine this library is developed on. It is only ever reachable through
 * NACK_RENDERER=test-fail.
 */
static bool nack__gfx_fail_init(struct nack_window *window)
{
    (void)window;
    return nack__error("this renderer fails on purpose");
}

static void nack__gfx_fail_shutdown(void)
{
}

static const struct nack_gfx_backend nack__gfx_fail_backend = {
    "test-fail",
    nack__gfx_fail_init,
    nack__gfx_fail_shutdown,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

bool nack__gfx_init(struct nack_window *window)
{
    const struct nack_gfx_backend *candidates[3];
    const char *preferred = getenv("NACK_RENDERER");
    char reason[256];
    size_t count = 0, i, j;

    reason[0] = '\0';

    if (preferred && !*preferred)
        preferred = NULL;

    if (preferred && strcmp(preferred, "test-fail") == 0)
        candidates[count++] = &nack__gfx_fail_backend;
#if defined(__APPLE__) && !defined(NACK_MACOS_OPENGL_ONLY)
    candidates[count++] = nack__gfx_backend_metal();
#endif
    candidates[count++] = nack__gfx_backend_gl();

    /* A named renderer goes first; the rest stay as fallbacks behind it. */
    if (preferred) {
        for (i = 0; i < count; ++i) {
            const struct nack_gfx_backend *chosen = candidates[i];

            if (!chosen || strcmp(chosen->name, preferred) != 0)
                continue;
            for (j = i; j > 0; --j)
                candidates[j] = candidates[j - 1];
            candidates[0] = chosen;
            break;
        }
        if (i == count)
            nack__log("nack: NACK_RENDERER=%s names no renderer this build "
                      "has; ignoring it", preferred);
    }

    for (i = 0; i < count; ++i) {
        const char *why;

        if (!candidates[i])
            continue;
        nack__gfx = candidates[i];
        if (nack__gfx->init(window)) {
            /*
             * A renderer we then fell back from left its complaint behind;
             * the caller only cares that one of them worked.
             */
            nack__clear_error();
            nack__log("nack: rendering with %s", nack__gfx->name);
            return true;
        }

        why = nack_get_error();
        nack__log("nack: the %s renderer is unavailable: %s", nack__gfx->name,
                  why ? why : "no reason given");
        /*
         * Keep the last renderer's complaint. If every candidate fails, that
         * is the only account of why anywhere, and "no renderer could be
         * started" on its own tells a user nothing they can act on.
         */
        snprintf(reason, sizeof reason, "%s: %s", nack__gfx->name,
                 why ? why : "no reason given");
        /*
         * Every backend's shutdown copes with a half-built state, and has to
         * put the window back as it found it so the next one can have it.
         */
        nack__gfx->shutdown();
        nack__gfx = NULL;
    }

    return nack__error("no renderer could be started (%s)",
                       reason[0] ? reason : "none were compiled in");
}

void nack__gfx_shutdown(void)
{
    if (nack__gfx)
        nack__gfx->shutdown();
    nack__gfx = NULL;
}

const char *nack__gfx_name(void)
{
    return nack__gfx ? nack__gfx->name : "none";
}

static int nack__gfx_failed_textures;

void nack__debug_fail_next_textures(int count)
{
    nack__gfx_failed_textures = count;
}

struct nack_texture *nack__gfx_texture_create(const uint8_t *rgba, int width,
                                              int height)
{
    if (nack__gfx_failed_textures > 0) {
        --nack__gfx_failed_textures;
        nack__error("texture creation failed on purpose");
        return NULL;
    }
    if (!nack__gfx) {
        nack__error("no renderer is active");
        return NULL;
    }
    return nack__gfx->texture_create(rgba, width, height);
}

void nack__gfx_texture_destroy(struct nack_texture *texture)
{
    if (nack__gfx && texture)
        nack__gfx->texture_destroy(texture);
}

void nack__gfx_begin_frame(struct nack_color clear, int fb_width, int fb_height,
                           int viewport_x, int viewport_y, int viewport_w,
                           int viewport_h)
{
    if (nack__gfx)
        nack__gfx->begin_frame(clear, fb_width, fb_height, viewport_x,
                               viewport_y, viewport_w, viewport_h);
}

void nack__gfx_draw(const float *vertices, size_t vertex_count, int mode,
                    struct nack_texture *texture)
{
    if (nack__gfx)
        nack__gfx->draw(vertices, vertex_count, mode, texture);
}

void nack__gfx_end_frame(void)
{
    if (nack__gfx)
        nack__gfx->end_frame();
}

void nack__gfx_resize(int fb_width, int fb_height)
{
    if (nack__gfx)
        nack__gfx->resize(fb_width, fb_height);
}

void nack__gfx_set_vsync(bool vsync)
{
    if (nack__gfx)
        nack__gfx->set_vsync(vsync);
}

void nack__gfx_set_capture(bool capture)
{
    if (nack__gfx && nack__gfx->set_capture)
        nack__gfx->set_capture(capture);
}

bool nack__gfx_read_pixel(int x, int y, uint8_t rgba[4])
{
    return nack__gfx ? nack__gfx->read_pixel(x, y, rgba) : false;
}
