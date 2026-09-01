/*
 * Fallback client-side decorations for Wayland.
 *
 * xdg-decoration is optional and some compositors (Mutter most visibly) never
 * implement it, so a client that only ever asks for server-side decorations
 * ends up as a bare rectangle with no way to move, resize or close it. This
 * draws a plain title bar and resize borders in that case.
 *
 * The decorations are four wl_subsurfaces filled with solid colour through
 * wl_shm. That is deliberately minimal: no fonts, no theming, no shadows. It
 * exists so a window is usable, not so it looks native.
 */
/* memfd_create and the file sealing constants are GNU extensions. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE 1
#endif

#include "nack_wayland.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define NACK_DECOR_TITLEBAR 28
#define NACK_DECOR_BORDER    5
#define NACK_DECOR_BUTTON   24   /* clickable width of the close button */

/* ARGB8888, premultiplied alpha (opaque here, so no premultiplication). */
#define NACK_DECOR_ACTIVE     0xFF2E3440u
#define NACK_DECOR_INACTIVE   0xFF3B4252u
#define NACK_DECOR_BORDER_COL 0xFF4C566Au
#define NACK_DECOR_GLYPH      0xFFD8DEE9u
#define NACK_DECOR_CLOSE_HOT  0xFFBF616Au

int nack__wl_decor_titlebar_height(const struct nack_window *w)
{
    (void)w;
    return NACK_DECOR_TITLEBAR;
}

int nack__wl_decor_border(const struct nack_window *w)
{
    (void)w;
    return NACK_DECOR_BORDER;
}

/* ------------------------------------------------------------------ */
/* Shared memory buffers                                              */
/* ------------------------------------------------------------------ */

static int nack__wl_create_shm_file(size_t size)
{
    int fd = -1;

#if defined(__linux__)
    fd = memfd_create("nack-decor", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd >= 0)
        fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);
#endif

    if (fd < 0) {
        /* Portable fallback: a file in the runtime dir, unlinked immediately. */
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (!runtime_dir)
            return -1;
        char path[256];
        snprintf(path, sizeof path, "%s/nack-decor-XXXXXX", runtime_dir);
        fd = mkstemp(path);
        if (fd < 0)
            return -1;
        unlink(path);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }

    if (ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void nack__wl_decor_release_buffer(struct nack_wl_decor *decor)
{
    if (decor->buffer) {
        wl_buffer_destroy(decor->buffer);
        decor->buffer = NULL;
    }
    if (decor->pixels) {
        munmap(decor->pixels, decor->size);
        decor->pixels = NULL;
        decor->size = 0;
    }
}

static bool nack__wl_decor_alloc(struct nack_wl_decor *decor, int width, int height)
{
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    if (decor->buffer && decor->width == width && decor->height == height)
        return true;

    nack__wl_decor_release_buffer(decor);

    size_t stride = (size_t)width * 4;
    size_t size = stride * (size_t)height;

    int fd = nack__wl_create_shm_file(size);
    if (fd < 0)
        return false;

    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return false;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(nack__wl.shm, fd, (int32_t)size);
    close(fd);
    if (!pool) {
        munmap(map, size);
        return false;
    }

    decor->buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
                                              (int32_t)stride,
                                              WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    if (!decor->buffer) {
        munmap(map, size);
        return false;
    }

    decor->pixels = (uint32_t *)map;
    decor->size = size;
    decor->width = width;
    decor->height = height;
    return true;
}

static void nack__wl_fill(struct nack_wl_decor *decor, uint32_t colour)
{
    if (!decor->pixels)
        return;
    size_t count = (size_t)decor->width * (size_t)decor->height;
    for (size_t i = 0; i < count; ++i)
        decor->pixels[i] = colour;
}

static void nack__wl_plot(struct nack_wl_decor *decor, int x, int y, uint32_t colour)
{
    if (!decor->pixels || x < 0 || y < 0 || x >= decor->width || y >= decor->height)
        return;
    decor->pixels[(size_t)y * (size_t)decor->width + (size_t)x] = colour;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static void nack__wl_draw_close_button(struct nack_window *w, struct nack_wl_decor *decor)
{
    struct nack_wl_window *ww = nack__wl_win(w);

    int box = NACK_DECOR_BUTTON;
    int left = decor->width - box - (NACK_DECOR_TITLEBAR - box) / 2;
    int top = (NACK_DECOR_TITLEBAR - box) / 2;

    if (ww->decor_close_hover) {
        for (int y = top; y < top + box; ++y)
            for (int x = left; x < left + box; ++x)
                nack__wl_plot(decor, x, y, NACK_DECOR_CLOSE_HOT);
    }

    /* A two-pixel-wide X, drawn directly rather than pulling in a font. */
    int inset = 7;
    for (int i = inset; i < box - inset; ++i) {
        for (int thickness = 0; thickness < 2; ++thickness) {
            nack__wl_plot(decor, left + i + thickness, top + i, NACK_DECOR_GLYPH);
            nack__wl_plot(decor, left + box - 1 - i + thickness, top + i,
                          NACK_DECOR_GLYPH);
        }
    }
}

void nack__wl_decor_redraw(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww->client_side_decorations)
        return;

    uint32_t title_colour = w->focused ? NACK_DECOR_ACTIVE : NACK_DECOR_INACTIVE;

    for (int i = 0; i < NACK_WL_DECOR_COUNT; ++i) {
        struct nack_wl_decor *decor = &ww->decor[i];
        if (!decor->surface || !decor->buffer)
            continue;

        nack__wl_fill(decor, i == NACK_WL_DECOR_TOP ? title_colour
                                                    : NACK_DECOR_BORDER_COL);
        if (i == NACK_WL_DECOR_TOP)
            nack__wl_draw_close_button(w, decor);

        wl_surface_attach(decor->surface, decor->buffer, 0, 0);
        wl_surface_damage_buffer(decor->surface, 0, 0, decor->width, decor->height);
        wl_surface_commit(decor->surface);
    }
}

/* ------------------------------------------------------------------ */
/* Geometry                                                           */
/* ------------------------------------------------------------------ */

void nack__wl_decor_resize(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww->client_side_decorations)
        return;

    const int title = NACK_DECOR_TITLEBAR;
    const int border = NACK_DECOR_BORDER;
    const int width = w->width;
    const int height = w->height;

    struct { int x, y, w, h; } layout[NACK_WL_DECOR_COUNT] = {
        /* TOP    */ { -border, -title,  width + 2 * border, title },
        /* BOTTOM */ { -border, height,  width + 2 * border, border },
        /* LEFT   */ { -border, 0,       border,             height },
        /* RIGHT  */ { width,   0,       border,             height },
    };

    for (int i = 0; i < NACK_WL_DECOR_COUNT; ++i) {
        struct nack_wl_decor *decor = &ww->decor[i];
        if (!decor->surface)
            continue;
        if (!nack__wl_decor_alloc(decor, layout[i].w, layout[i].h))
            continue;
        wl_subsurface_set_position(decor->subsurface, layout[i].x, layout[i].y);
    }

    /*
     * The window geometry tells the compositor which part of the surface tree
     * is the window proper, so snapping and maximised sizing account for the
     * decorations we drew.
     */
    if (ww->xdg_surface)
        xdg_surface_set_window_geometry(ww->xdg_surface, -border, -title,
                                        width + 2 * border, height + title + border);

    nack__wl_decor_redraw(w);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                           */
/* ------------------------------------------------------------------ */

bool nack__wl_decor_enable(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (ww->client_side_decorations)
        return true;
    if (!nack__wl.shm || !nack__wl.subcompositor || !w->decorated)
        return false;

    for (int i = 0; i < NACK_WL_DECOR_COUNT; ++i) {
        struct nack_wl_decor *decor = &ww->decor[i];
        decor->surface = wl_compositor_create_surface(nack__wl.compositor);
        if (!decor->surface)
            goto fail;

        decor->subsurface = wl_subcompositor_get_subsurface(
            nack__wl.subcompositor, decor->surface, ww->surface);
        if (!decor->subsurface)
            goto fail;

        /* Desynchronised so a decoration repaint does not need a commit of
         * the content surface, which is driven by the render loop. */
        wl_subsurface_set_desync(decor->subsurface);

        /* Decorations never take input focus for text; they only need
         * pointer events, so the input region stays default (whole surface)
         * while the opaque region lets the compositor skip blending. */
        struct wl_region *opaque = wl_compositor_create_region(nack__wl.compositor);
        if (opaque) {
            wl_region_add(opaque, 0, 0, INT32_MAX, INT32_MAX);
            wl_surface_set_opaque_region(decor->surface, opaque);
            wl_region_destroy(opaque);
        }
    }

    ww->client_side_decorations = true;
    nack__wl_decor_resize(w);
    nack__log("nack: compositor has no xdg-decoration support; "
              "drawing fallback client-side decorations");
    return true;

fail:
    nack__wl_decor_destroy(w);
    return false;
}

void nack__wl_decor_destroy(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww)
        return;

    for (int i = 0; i < NACK_WL_DECOR_COUNT; ++i) {
        struct nack_wl_decor *decor = &ww->decor[i];
        nack__wl_decor_release_buffer(decor);
        if (decor->subsurface) {
            wl_subsurface_destroy(decor->subsurface);
            decor->subsurface = NULL;
        }
        if (decor->surface) {
            wl_surface_destroy(decor->surface);
            decor->surface = NULL;
        }
    }
    ww->client_side_decorations = false;

    if (nack__wl.decor_focus == w)
        nack__wl.decor_focus = NULL;
}

bool nack__wl_decor_find(struct wl_surface *surface, struct nack_window **out_window,
                         enum nack_wl_decor_part *out_part)
{
    if (!surface)
        return false;
    for (size_t i = 0; i < nack__g.window_count; ++i) {
        struct nack_window *w = nack__g.windows[i];
        struct nack_wl_window *ww = (struct nack_wl_window *)w->native;
        if (!ww || !ww->client_side_decorations)
            continue;
        for (int part = 0; part < NACK_WL_DECOR_COUNT; ++part) {
            if (ww->decor[part].surface == surface) {
                *out_window = w;
                *out_part = (enum nack_wl_decor_part)part;
                return true;
            }
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Pointer interaction                                                */
/* ------------------------------------------------------------------ */

static bool nack__wl_decor_in_close_button(struct nack_window *w, double x, double y)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (nack__wl.decor_focus_part != NACK_WL_DECOR_TOP)
        return false;

    const struct nack_wl_decor *decor = &ww->decor[NACK_WL_DECOR_TOP];
    int box = NACK_DECOR_BUTTON;
    int left = decor->width - box - (NACK_DECOR_TITLEBAR - box) / 2;
    int top = (NACK_DECOR_TITLEBAR - box) / 2;

    return x >= left && x < left + box && y >= top && y < top + box;
}

void nack__wl_decor_pointer_motion(struct nack_window *w, double x, double y)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    nack__wl.decor_x = x;
    nack__wl.decor_y = y;

    bool hover = nack__wl_decor_in_close_button(w, x, y);
    if (hover != ww->decor_close_hover) {
        ww->decor_close_hover = hover;
        nack__wl_decor_redraw(w);
    }
}

void nack__wl_decor_pointer_leave(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (ww && ww->decor_close_hover) {
        ww->decor_close_hover = false;
        nack__wl_decor_redraw(w);
    }
}

/* Maps a position on a border surface to the resize edge it represents. */
static uint32_t nack__wl_decor_resize_edge(struct nack_window *w, enum nack_wl_decor_part part,
                                           double x, double y)
{
    const int corner = 16;

    switch (part) {
    case NACK_WL_DECOR_TOP:
        if (y >= NACK_DECOR_BORDER)
            return 0;    /* below the resize strip: this is the drag area */
        if (x < corner)  return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
        if (x > w->width + NACK_DECOR_BORDER) return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP;

    case NACK_WL_DECOR_BOTTOM:
        if (x < corner) return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
        if (x > w->width + NACK_DECOR_BORDER)
            return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;

    case NACK_WL_DECOR_LEFT:
        if (y < corner) return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
        if (y > w->height - corner) return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
        return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;

    case NACK_WL_DECOR_RIGHT:
        if (y < corner) return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
        if (y > w->height - corner) return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
        return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;

    default:
        return 0;
    }
}

bool nack__wl_decor_pointer_button(struct nack_window *w, int button, bool down,
                                   uint32_t serial)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww->client_side_decorations || !down)
        return false;

    double x = nack__wl.decor_x;
    double y = nack__wl.decor_y;
    enum nack_wl_decor_part part = nack__wl.decor_focus_part;

    if (button == NACK_MOUSE_RIGHT) {
        if (nack__wl.seat)
            xdg_toplevel_show_window_menu(ww->xdg_toplevel, nack__wl.seat, serial,
                                          (int32_t)x, (int32_t)y);
        return true;
    }

    if (button != NACK_MOUSE_LEFT)
        return false;

    if (nack__wl_decor_in_close_button(w, x, y)) {
        w->should_close = true;
        nack__emit_simple(w, NACK_EVENT_WINDOW_CLOSE);
        return true;
    }

    if (w->resizable) {
        uint32_t edge = nack__wl_decor_resize_edge(w, part, x, y);
        if (edge != 0) {
            xdg_toplevel_resize(ww->xdg_toplevel, nack__wl.seat, serial, edge);
            return true;
        }
    }

    if (part == NACK_WL_DECOR_TOP) {
        /* Double-click on the title bar toggles maximise, as everywhere else. */
        if (w->click_count >= 2) {
            if (w->maximized)
                xdg_toplevel_unset_maximized(ww->xdg_toplevel);
            else
                xdg_toplevel_set_maximized(ww->xdg_toplevel);
        } else {
            xdg_toplevel_move(ww->xdg_toplevel, nack__wl.seat, serial);
        }
        return true;
    }

    return false;
}
