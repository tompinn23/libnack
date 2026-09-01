/*
 * The interface between the console and whatever draws it.
 *
 * There are two implementations: OpenGL 3.3 (nack_gfx_gl.c) everywhere, and
 * Metal (nack_gfx_metal.m) on macOS, where OpenGL is deprecated and capped at
 * 4.1. Exactly one is compiled in, chosen by the build.
 *
 * The console layer above this knows nothing about either API: it produces
 * quads and hands them over.
 */
#ifndef NACK_GFX_H_INCLUDED
#define NACK_GFX_H_INCLUDED

#include "../nack_internal.h"

/* position.xy, uv.xy, fg.rgba, bg.rgba */
#define NACK_FLOATS_PER_VERTEX 12
#define NACK_VERTICES_PER_CELL 6

/* An atlas, whatever the backend calls one. */
struct nack_texture;

/*
 * Brings up the device, swap chain and pipeline for the window. The window
 * has already been created; on the OpenGL path a context is made current here,
 * and on the Metal path a CAMetalLayer is attached.
 */
bool nack__gfx_init(struct nack_window *window);
void nack__gfx_shutdown(void);

/* Uploads an RGBA8 atlas. Returns NULL and sets the error on failure. */
struct nack_texture *nack__gfx_texture_create(const uint8_t *rgba, int width,
                                              int height);
void nack__gfx_texture_destroy(struct nack_texture *texture);

/*
 * A frame is: begin, then any number of draws, then end. The clear colour
 * fills the whole framebuffer, so it is what shows in the letterbox; the
 * viewport is where the console itself goes, in framebuffer pixels.
 */
void nack__gfx_begin_frame(struct nack_color clear, int fb_width, int fb_height,
                           int viewport_x, int viewport_y,
                           int viewport_w, int viewport_h);

/*
 * Draws triangles. mode 0 takes the colour from each vertex's background and
 * ignores the texture, which is how cell backgrounds are filled; mode 1
 * samples the texture and tints it by the foreground.
 */
void nack__gfx_draw(const float *vertices, size_t vertex_count, int mode,
                    struct nack_texture *texture);

void nack__gfx_end_frame(void);

/* Called when the framebuffer changes size. */
void nack__gfx_resize(int fb_width, int fb_height);

/* Vertical sync. */
void nack__gfx_set_vsync(bool vsync);

/*
 * Reads one framebuffer pixel back, for the tests. Returns false where the
 * backend cannot do it; callers treat that as "not checkable" rather than as
 * a failure.
 */
bool nack__gfx_read_pixel(int x, int y, uint8_t rgba[4]);

/* Name of the backend actually in use, for diagnostics. */
const char *nack__gfx_name(void);

#endif /* NACK_GFX_H_INCLUDED */
