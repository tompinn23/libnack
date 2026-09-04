/*
 * The interface between the console and whatever draws it.
 *
 * There are two implementations: OpenGL 3.3 (nack_gfx_gl.c) everywhere, and
 * Metal (nack_gfx_metal.m) on macOS, where OpenGL is deprecated and capped at
 * 4.1. On macOS both are compiled in and selected at run time, so a Metal
 * failure falls back to OpenGL rather than leaving the library with nothing to
 * draw with. Elsewhere there is only OpenGL and the indirection costs a handful
 * of calls per frame.
 *
 * The console layer above this knows nothing about either API: it produces
 * quads and hands them over.
 */
#ifndef NACK_GFX_H_INCLUDED
#define NACK_GFX_H_INCLUDED

#include "../nack_internal.h"

/*
 * C++ only. The dispatch below is a class, not a table of function pointers:
 * see the comment on nack_gfx_backend. The console layer's own entry points
 * further down keep C linkage, as they always did.
 */
#ifndef __cplusplus
#  error "nack_gfx.h is C++"
#endif

#include <cstddef>
#include <cstdint>

/* position.xy, uv.xy, fg.rgba, bg.rgba */
#define NACK_FLOATS_PER_VERTEX 12
#define NACK_VERTICES_PER_CELL 6

/* An atlas, whatever the backend calls one. */
struct nack_texture;

/*
 * One renderer.
 *
 * Still chosen at run time - that is not an implementation detail but the
 * point: on macOS a failing Metal has to fall back to OpenGL, and
 * NACK_RENDERER picks between them, so the choice cannot be made at build
 * time and a template cannot express it.
 *
 * What it is not any more is a struct of function pointers filled in
 * positionally. Every operation a renderer must provide is pure virtual, so a
 * backend that forgets one does not compile, and reordering them cannot
 * silently rewire an existing backend. The two that are genuinely optional
 * say so by having a default here, which is the distinction the table could
 * not draw: a NULL slot meant either "not supported" or "forgotten", and
 * callers checked some of them and not others.
 */
class nack_gfx_backend {
public:
    virtual ~nack_gfx_backend() = default;

    virtual const char *name() const = 0;

    virtual bool init(nack_window *window) = 0;
    virtual void shutdown() = 0;

    virtual nack_texture *texture_create(const uint8_t *rgba, int width,
                                                int height) = 0;
    virtual void texture_destroy(nack_texture *texture) = 0;

    virtual void begin_frame(nack_color clear, int fb_width,
                             int fb_height, int viewport_x, int viewport_y,
                             int viewport_w, int viewport_h) = 0;
    virtual void draw(const float *vertices, size_t vertex_count, int mode,
                      nack_texture *texture) = 0;
    virtual void end_frame() = 0;

    virtual void resize(int fb_width, int fb_height) = 0;
    virtual void set_vsync(bool vsync) = 0;

    /*
     * Keeping a copy of each frame so the tests can read pixels back. A
     * renderer that cannot do it declines by not overriding, and read_pixel
     * then reports failure rather than being a null call nobody guarded.
     */
    virtual void set_capture(bool capture) { (void)capture; }
    virtual bool read_pixel(int x, int y, uint8_t rgba[4])
    {
        (void)x; (void)y; (void)rgba;
        return false;
    }
};

nack_gfx_backend *nack__gfx_backend_gl(void);
#if defined(__APPLE__)
nack_gfx_backend *nack__gfx_backend_metal(void);
#endif

extern "C" {

/*
 * Brings up the device, swap chain and pipeline for the window. The window
 * has already been created; on the OpenGL path a context is made current here,
 * and on the Metal path a CAMetalLayer is attached.
 */
bool nack__gfx_init(nack_window *window);
void nack__gfx_shutdown(void);

/* Uploads an RGBA8 atlas. Returns NULL and sets the error on failure. */
nack_texture *nack__gfx_texture_create(const uint8_t *rgba, int width,
                                              int height);
void nack__gfx_texture_destroy(nack_texture *texture);

/*
 * A frame is: begin, then any number of draws, then end. The clear colour
 * fills the whole framebuffer, so it is what shows in the letterbox; the
 * viewport is where the console itself goes, in framebuffer pixels.
 */
void nack__gfx_begin_frame(nack_color clear, int fb_width, int fb_height,
                           int viewport_x, int viewport_y,
                           int viewport_w, int viewport_h);

/*
 * Draws triangles. mode 0 takes the colour from each vertex's background and
 * ignores the texture, which is how cell backgrounds are filled; mode 1
 * samples the texture and tints it by the foreground.
 */
void nack__gfx_draw(const float *vertices, size_t vertex_count, int mode,
                    nack_texture *texture);

void nack__gfx_end_frame(void);

/* Called when the framebuffer changes size. */
void nack__gfx_resize(int fb_width, int fb_height);

/* Vertical sync. */
void nack__gfx_set_vsync(bool vsync);

/*
 * Keeps a copy of each frame so it can be read back afterwards. Off by
 * default: it costs a full framebuffer read every frame, and only the tests
 * want it. It has to exist because a frame is gone once it has been
 * presented - the OpenGL back buffer is undefined after a swap, whatever a
 * given driver happens to leave in it.
 */
void nack__gfx_set_capture(bool capture);

/*
 * Reads one pixel of the last frame back, for the tests. Returns false where
 * the backend cannot do it, or where capture was never turned on; callers
 * treat that as "not checkable" rather than as a failure.
 */
bool nack__gfx_read_pixel(int x, int y, uint8_t rgba[4]);

/* Name of the backend actually in use, for diagnostics. */
const char *nack__gfx_name(void);


}   /* extern "C" */
#endif /* NACK_GFX_H_INCLUDED */
