/*
 * Fallback client-side decorations for Wayland, and the placeholder frame that
 * maps a window before the client has drawn anything.
 *
 * xdg-decoration is optional and some compositors (Mutter and WSLg among them)
 * never implement it, so a client that only asks for server-side decorations
 * ends up as a bare rectangle with no way to move, resize or close it. This
 * draws a title bar with buttons and resize borders in that case.
 *
 * Four wl_subsurfaces filled through wl_shm, with an 8x8 bitmap font for the
 * title. It is deliberately plain: no theming, no shadows, no rounded corners.
 * It exists so a window is usable, not so it passes for native.
 */
/* memfd_create and the file sealing constants are GNU extensions. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE 1
#endif

#include "nack_wayland.h"
#include "nack_wayland_font.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define NACK_DECOR_TITLEBAR 30
#define NACK_DECOR_BORDER    5
#define NACK_DECOR_BUTTON   28   /* clickable width of one title bar button */
#define NACK_DECOR_CORNER   16   /* corner hit region along each edge       */
#define NACK_DECOR_TEXT_PAD  8

/* ARGB8888. Everything here is opaque, so no premultiplication is needed. */
#define NACK_DECOR_ACTIVE      0xFF2E3440u
#define NACK_DECOR_INACTIVE    0xFF3B4252u
#define NACK_DECOR_BORDER_COL  0xFF4C566Au
#define NACK_DECOR_TEXT        0xFFE5E9F0u
#define NACK_DECOR_TEXT_DIM    0xFF8892A4u
#define NACK_DECOR_GLYPH       0xFFD8DEE9u
#define NACK_DECOR_HOVER       0xFF4C566Au
#define NACK_DECOR_CLOSE_HOVER 0xFFBF616Au

int nack__wl_decor_titlebar_height(const nack_window *w)
{
    (void)w;
    return NACK_DECOR_TITLEBAR;
}

int nack__wl_decor_border(const nack_window *w)
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
    fd = memfd_create("nack-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd >= 0)
        fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);
#endif

    if (fd < 0) {
        /* Portable fallback: a file in the runtime dir, unlinked at once. */
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (!runtime_dir)
            return -1;
        char path[256];
        snprintf(path, sizeof path, "%s/nack-shm-XXXXXX", runtime_dir);
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

void nack__wl_shm_buffer_release(nack_wl_shm_buffer *buf)
{
    if (buf->buffer) {
        wl_buffer_destroy(buf->buffer);
        buf->buffer = nullptr;
    }
    if (buf->pixels) {
        munmap(buf->pixels, buf->size);
        buf->pixels = nullptr;
        buf->size = 0;
    }
    buf->width = 0;
    buf->height = 0;
}

bool nack__wl_shm_buffer_alloc(nack_wl_shm_buffer *buf, int width,
                               int height)
{
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    if (buf->buffer && buf->width == width && buf->height == height)
        return true;

    nack__wl_shm_buffer_release(buf);

    if (!nack__wl.shm)
        return false;

    size_t stride = (size_t)width * 4;
    size_t size = stride * (size_t)height;

    int fd = nack__wl_create_shm_file(size);
    if (fd < 0)
        return false;

    void *map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
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

    buf->buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
                                            (int32_t)stride,
                                            WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    if (!buf->buffer) {
        munmap(map, size);
        return false;
    }

    buf->pixels = (uint32_t *)map;
    buf->size = size;
    buf->width = width;
    buf->height = height;
    return true;
}

static void nack__wl_fill(nack_wl_shm_buffer *buf, uint32_t colour)
{
    size_t count, i;
    if (!buf->pixels)
        return;
    count = (size_t)buf->width * (size_t)buf->height;
    for (i = 0; i < count; ++i)
        buf->pixels[i] = colour;
}

/* Fills a rectangle given in logical pixels, scaled to device pixels. */
static void nack__wl_fill_rect(nack_wl_shm_buffer *buf, int scale,
                               int x, int y, int width, int height,
                               uint32_t colour)
{
    int px, py;
    if (!buf->pixels)
        return;
    for (py = y * scale; py < (y + height) * scale; ++py) {
        if (py < 0 || py >= buf->height)
            continue;
        for (px = x * scale; px < (x + width) * scale; ++px) {
            if (px < 0 || px >= buf->width)
                continue;
            buf->pixels[(size_t)py * (size_t)buf->width + (size_t)px] = colour;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Placeholder frame                                                  */
/* ------------------------------------------------------------------ */

/*
 * A surface with no committed buffer is never mapped, so a window that has
 * not rendered yet does not appear at all - where X11 and Win32 would show an
 * empty one. Committing a blank frame makes nack_window::show() mean the same
 * thing on every backend. The client's first real frame replaces it.
 */
void nack__wl_present_placeholder(nack_window *w)
{
    nack_wl_window *ww = nack__wl_win(w);
    int scale;

    if (!ww || ww->presented || !nack__wl.shm || !ww->surface)
        return;

    scale = ww->buffer_scale > 0 ? ww->buffer_scale : 1;

    if (!nack__wl_shm_buffer_alloc(&ww->placeholder, w->width * scale,
                                   w->height * scale))
        return;

    /* Transparent windows must not flash opaque black before the first frame. */
    nack__wl_fill(&ww->placeholder, w->transparent ? 0x00000000u : 0xFF000000u);

    wl_surface_set_buffer_scale(ww->surface, scale);
    wl_surface_attach(ww->surface, ww->placeholder.buffer, 0, 0);
    wl_surface_damage_buffer(ww->surface, 0, 0, ww->placeholder.width,
                             ww->placeholder.height);
    wl_surface_commit(ww->surface);
}

void nack__wl_drop_placeholder(nack_window *w)
{
    nack_wl_window *ww = nack__wl_win(w);
    if (!ww)
        return;
    ww->presented = true;
    /* The compositor has the client's own buffer now, so this can go. */
    nack__wl_shm_buffer_release(&ww->placeholder);
}

/* ------------------------------------------------------------------ */
/* Text                                                               */
/* ------------------------------------------------------------------ */

static void nack__wl_draw_glyph(nack_wl_shm_buffer *buf, int scale,
                                int x, int y, unsigned char ch, uint32_t colour)
{
    int row, col, sx, sy;
    const uint8_t *bitmap;

    if (ch < NACK_FONT_FIRST || ch > NACK_FONT_LAST)
        ch = '?';
    bitmap = nack__wl_font[ch - NACK_FONT_FIRST];

    for (row = 0; row < NACK_FONT_H; ++row) {
        for (col = 0; col < NACK_FONT_W; ++col) {
            if (!(bitmap[row] & (1u << col)))
                continue;
            /* One font pixel becomes a scale x scale block. */
            for (sy = 0; sy < scale; ++sy) {
                int py = (y + row) * scale + sy;
                if (py < 0 || py >= buf->height)
                    continue;
                for (sx = 0; sx < scale; ++sx) {
                    int px = (x + col) * scale + sx;
                    if (px < 0 || px >= buf->width)
                        continue;
                    buf->pixels[(size_t)py * (size_t)buf->width +
                                (size_t)px] = colour;
                }
            }
        }
    }
}

/*
 * Draws as much of the title as fits, ellipsising when it does not. The font
 * is ASCII, so any multi-byte sequence collapses to a single '?' rather than
 * rendering as mojibake.
 */
static void nack__wl_draw_title(nack_wl_shm_buffer *buf, int scale,
                                const char *title, int x, int y, int max_width,
                                uint32_t colour)
{
    int columns = max_width / NACK_FONT_W;
    int drawn = 0;
    const unsigned char *p;

    if (columns <= 0 || !title)
        return;

    for (p = (const unsigned char *)title; *p && drawn < columns; ++p) {
        unsigned char ch = *p;

        if (ch >= 0x80) {
            /* Skip the continuation bytes of this UTF-8 sequence. */
            while (p[1] && (p[1] & 0xC0) == 0x80)
                ++p;
            ch = '?';
        }

        /* Three characters from the end, switch to an ellipsis if more
         * text remains than will fit. */
        if (drawn == columns - 1 && p[1] != '\0')
            ch = '.';

        nack__wl_draw_glyph(buf, scale, x + drawn * NACK_FONT_W, y, ch, colour);
        drawn++;
    }
}

/* ------------------------------------------------------------------ */
/* Button geometry                                                    */
/* ------------------------------------------------------------------ */

/* Buttons are right aligned: minimize, maximize, close. */
static int nack__wl_button_x(const nack_wl_decor *top,
                             nack_wl_decor_button button)
{
    int from_right = (NACK_WL_BUTTON_COUNT - 1 - (int)button) + 1;
    return top->width - from_right * NACK_DECOR_BUTTON;
}

static nack_wl_decor_button nack__wl_button_at(const nack_window *w,
                                                    double x, double y)
{
    const nack_wl_window *ww = (const nack_wl_window *)w->native;
    const nack_wl_decor *top = &ww->decor[NACK_WL_DECOR_TOP];
    int i;

    if (nack__wl.decor_focus_part != NACK_WL_DECOR_TOP)
        return NACK_WL_BUTTON_NONE;
    if (y < 0 || y >= NACK_DECOR_TITLEBAR)
        return NACK_WL_BUTTON_NONE;

    for (i = 0; i < NACK_WL_BUTTON_COUNT; ++i) {
        int left = nack__wl_button_x(top, (nack_wl_decor_button)i);
        if (x >= left && x < left + NACK_DECOR_BUTTON)
            return (nack_wl_decor_button)i;
    }
    return NACK_WL_BUTTON_NONE;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static void nack__wl_draw_button(nack_window *w,
                                 nack_wl_decor *top,
                                 nack_wl_decor_button button)
{
    nack_wl_window *ww = nack__wl_win(w);
    const int scale = ww->decor_scale;
    const int left = nack__wl_button_x(top, button);
    const int centre_x = left + NACK_DECOR_BUTTON / 2;
    const int centre_y = NACK_DECOR_TITLEBAR / 2;
    const uint32_t glyph = w->focused ? NACK_DECOR_GLYPH : NACK_DECOR_TEXT_DIM;
    int i;

    if (ww->decor_hover == button)
        nack__wl_fill_rect(&top->buf, scale, left, 0, NACK_DECOR_BUTTON,
                           NACK_DECOR_TITLEBAR,
                           button == NACK_WL_BUTTON_CLOSE ? NACK_DECOR_CLOSE_HOVER
                                                          : NACK_DECOR_HOVER);

    switch (button) {
    case NACK_WL_BUTTON_MINIMIZE:
        /* A horizontal bar. */
        nack__wl_fill_rect(&top->buf, scale, centre_x - 5, centre_y + 3, 10, 2, glyph);
        break;

    case NACK_WL_BUTTON_MAXIMIZE:
        /* An outlined square, doubled when already maximised. */
        nack__wl_fill_rect(&top->buf, scale, centre_x - 5, centre_y - 5, 10, 2, glyph);
        nack__wl_fill_rect(&top->buf, scale, centre_x - 5, centre_y + 3, 10, 2, glyph);
        nack__wl_fill_rect(&top->buf, scale, centre_x - 5, centre_y - 5, 2, 10, glyph);
        nack__wl_fill_rect(&top->buf, scale, centre_x + 3, centre_y - 5, 2, 10, glyph);
        if (w->maximized) {
            nack__wl_fill_rect(&top->buf, scale, centre_x - 2, centre_y - 8, 8, 2, glyph);
            nack__wl_fill_rect(&top->buf, scale, centre_x + 6, centre_y - 8, 2, 8, glyph);
        }
        break;

    case NACK_WL_BUTTON_CLOSE:
        /* Two diagonals, two pixels thick. */
        for (i = -5; i <= 5; ++i) {
            nack__wl_fill_rect(&top->buf, scale, centre_x + i, centre_y + i, 2, 2, glyph);
            nack__wl_fill_rect(&top->buf, scale, centre_x + i, centre_y - i, 2, 2, glyph);
        }
        break;

    default:
        break;
    }
}

void nack__wl_decor_redraw(nack_window *w)
{
    nack_wl_window *ww = nack__wl_win(w);
    uint32_t title_colour, text_colour;
    int i;

    if (!ww || !ww->client_side_decorations)
        return;

    title_colour = w->focused ? NACK_DECOR_ACTIVE : NACK_DECOR_INACTIVE;
    text_colour = w->focused ? NACK_DECOR_TEXT : NACK_DECOR_TEXT_DIM;

    for (i = 0; i < NACK_WL_DECOR_COUNT; ++i) {
        nack_wl_decor *decor = &ww->decor[i];
        if (!decor->surface || !decor->buf.buffer)
            continue;

        nack__wl_fill(&decor->buf, i == NACK_WL_DECOR_TOP ? title_colour
                                                          : NACK_DECOR_BORDER_COL);

        if (i == NACK_WL_DECOR_TOP) {
            int buttons_width = NACK_WL_BUTTON_COUNT * NACK_DECOR_BUTTON;
            int text_width = decor->width - buttons_width -
                             NACK_DECOR_TEXT_PAD * 2;
            int text_y = (NACK_DECOR_TITLEBAR - NACK_FONT_H) / 2;
            int button;

            if (text_width > 0 && !w->title.empty())
                nack__wl_draw_title(&decor->buf, ww->decor_scale,
                                    w->title.c_str(),
                                    NACK_DECOR_BORDER + NACK_DECOR_TEXT_PAD,
                                    text_y, text_width, text_colour);

            for (button = 0; button < NACK_WL_BUTTON_COUNT; ++button)
                nack__wl_draw_button(w, decor,
                                     (nack_wl_decor_button)button);
        }

        wl_surface_set_buffer_scale(decor->surface, ww->decor_scale);
        wl_surface_attach(decor->surface, decor->buf.buffer, 0, 0);
        wl_surface_damage_buffer(decor->surface, 0, 0, decor->buf.width,
                                 decor->buf.height);
        wl_surface_commit(decor->surface);
    }
}

/* ------------------------------------------------------------------ */
/* Geometry                                                           */
/* ------------------------------------------------------------------ */

void nack__wl_decor_resize(nack_window *w)
{
    nack_wl_window *ww = nack__wl_win(w);
    const int title = NACK_DECOR_TITLEBAR;
    const int border = NACK_DECOR_BORDER;
    int width, height, i;
    struct { int x, y, w, h; } layout[NACK_WL_DECOR_COUNT];

    if (!ww || !ww->client_side_decorations)
        return;

    width = w->width;
    height = w->height;

    /* The side borders run the full height including the title bar, so the
     * top corners are reachable for a diagonal resize. */
    layout[NACK_WL_DECOR_TOP].x = -border;
    layout[NACK_WL_DECOR_TOP].y = -title;
    layout[NACK_WL_DECOR_TOP].w = width + 2 * border;
    layout[NACK_WL_DECOR_TOP].h = title;

    layout[NACK_WL_DECOR_BOTTOM].x = -border;
    layout[NACK_WL_DECOR_BOTTOM].y = height;
    layout[NACK_WL_DECOR_BOTTOM].w = width + 2 * border;
    layout[NACK_WL_DECOR_BOTTOM].h = border;

    layout[NACK_WL_DECOR_LEFT].x = -border;
    layout[NACK_WL_DECOR_LEFT].y = 0;
    layout[NACK_WL_DECOR_LEFT].w = border;
    layout[NACK_WL_DECOR_LEFT].h = height;

    layout[NACK_WL_DECOR_RIGHT].x = width;
    layout[NACK_WL_DECOR_RIGHT].y = 0;
    layout[NACK_WL_DECOR_RIGHT].w = border;
    layout[NACK_WL_DECOR_RIGHT].h = height;

    for (i = 0; i < NACK_WL_DECOR_COUNT; ++i) {
        nack_wl_decor *decor = &ww->decor[i];
        if (!decor->surface)
            continue;
        decor->width = layout[i].w;
        decor->height = layout[i].h;
        if (!nack__wl_shm_buffer_alloc(&decor->buf,
                                       layout[i].w * ww->decor_scale,
                                       layout[i].h * ww->decor_scale))
            continue;
        wl_subsurface_set_position(decor->subsurface, layout[i].x, layout[i].y);
    }

    /*
     * Tell the compositor which part of the surface tree is the window
     * proper, so snapping and maximised sizing account for the frame we drew.
     */
    if (ww->xdg_surface)
        xdg_surface_set_window_geometry(ww->xdg_surface, -border, -title,
                                        width + 2 * border,
                                        height + title + border);

    nack__wl_decor_redraw(w);
}

void nack__wl_decor_update_scale(nack_window *w, int scale)
{
    nack_wl_window *ww = nack__wl_win(w);
    if (!ww || scale < 1)
        return;
    if (ww->decor_scale == scale)
        return;
    ww->decor_scale = scale;
    if (ww->client_side_decorations)
        nack__wl_decor_resize(w);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                           */
/* ------------------------------------------------------------------ */

bool nack__wl_decor_enable(nack_window *w)
{
    nack_wl_window *ww = nack__wl_win(w);
    int i;

    if (!ww || ww->client_side_decorations)
        return ww != nullptr;
    if (!nack__wl.shm || !nack__wl.subcompositor || !w->decorated)
        return false;

    if (ww->decor_scale < 1)
        ww->decor_scale = ww->buffer_scale > 0 ? ww->buffer_scale : 1;
    ww->decor_hover = NACK_WL_BUTTON_NONE;

    for (i = 0; i < NACK_WL_DECOR_COUNT; ++i) {
        nack_wl_decor *decor = &ww->decor[i];
        struct wl_region *opaque;

        decor->surface = wl_compositor_create_surface(nack__wl.compositor);
        if (!decor->surface)
            goto fail;

        decor->subsurface = wl_subcompositor_get_subsurface(
            nack__wl.subcompositor, decor->surface, ww->surface);
        if (!decor->subsurface)
            goto fail;

        /* Desynchronised so a decoration repaint does not have to wait for a
         * commit of the content surface, which the render loop drives. */
        wl_subsurface_set_desync(decor->subsurface);
        wl_subsurface_place_below(decor->subsurface, ww->surface);

        /* The frame is fully opaque, which lets the compositor skip blending. */
        opaque = wl_compositor_create_region(nack__wl.compositor);
        if (opaque) {
            wl_region_add(opaque, 0, 0, INT32_MAX, INT32_MAX);
            wl_surface_set_opaque_region(decor->surface, opaque);
            wl_region_destroy(opaque);
        }
    }

    ww->client_side_decorations = true;
    nack__wl_decor_resize(w);
    nack_log("nack: compositor has no xdg-decoration support; "
              "drawing fallback client-side decorations");
    return true;

fail:
    nack__wl_decor_destroy(w);
    return false;
}

void nack__wl_decor_destroy(nack_window *w)
{
    nack_wl_window *ww = nack__wl_win(w);
    int i;

    if (!ww)
        return;

    for (i = 0; i < NACK_WL_DECOR_COUNT; ++i) {
        nack_wl_decor *decor = &ww->decor[i];
        nack__wl_shm_buffer_release(&decor->buf);
        if (decor->subsurface) {
            wl_subsurface_destroy(decor->subsurface);
            decor->subsurface = nullptr;
        }
        if (decor->surface) {
            wl_surface_destroy(decor->surface);
            decor->surface = nullptr;
        }
    }
    ww->client_side_decorations = false;

    if (nack__wl.decor_focus == w)
        nack__wl.decor_focus = nullptr;
}

bool nack__wl_decor_find(struct wl_surface *surface,
                         nack_window **out_window,
                         nack_wl_decor_part *out_part)
{
    size_t i;
    int part;

    if (!surface)
        return false;

    for (i = 0; i < state.windows.size(); ++i) {
        nack_window *w = state.windows[i];
        nack_wl_window *ww = (nack_wl_window *)w->native;
        if (!ww || !ww->client_side_decorations)
            continue;
        for (part = 0; part < NACK_WL_DECOR_COUNT; ++part) {
            if (ww->decor[part].surface == surface) {
                *out_window = w;
                *out_part = (nack_wl_decor_part)part;
                return true;
            }
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Resize edges                                                       */
/* ------------------------------------------------------------------ */

/* Maps a position on a decoration surface to the resize edge it represents,
 * or 0 where the position is a drag area rather than an edge. */
static uint32_t nack__wl_decor_resize_edge(nack_window *w,
                                           nack_wl_decor_part part,
                                           double x, double y)
{
    const nack_wl_window *ww = (const nack_wl_window *)w->native;
    const int corner = NACK_DECOR_CORNER;
    int width;

    if (!w->resizable)
        return 0;

    width = ww->decor[NACK_WL_DECOR_TOP].width;   /* window width + 2 borders */

    switch (part) {
    case NACK_WL_DECOR_TOP:
        /* Only the top strip resizes; the rest of the bar drags the window. */
        if (y >= NACK_DECOR_BORDER)
            return 0;
        if (x < corner)
            return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
        if (x >= width - corner)
            return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP;

    case NACK_WL_DECOR_BOTTOM:
        if (x < corner)
            return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
        if (x >= width - corner)
            return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;

    case NACK_WL_DECOR_LEFT:
        if (y < corner)
            return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
        if (y >= w->height - corner)
            return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
        return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;

    case NACK_WL_DECOR_RIGHT:
        if (y < corner)
            return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
        if (y >= w->height - corner)
            return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
        return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;

    default:
        return 0;
    }
}

static nack_cursor_shape nack__wl_cursor_for_edge(uint32_t edge)
{
    switch (edge) {
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP:
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:
        return NACK_CURSOR_RESIZE_V;
    case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
    case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
        return NACK_CURSOR_RESIZE_H;
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT:
        return NACK_CURSOR_RESIZE_NWSE;
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:
        return NACK_CURSOR_RESIZE_NESW;
    default:
        return NACK_CURSOR_ARROW;
    }
}

/* ------------------------------------------------------------------ */
/* Pointer interaction                                                */
/* ------------------------------------------------------------------ */

void nack__wl_decor_pointer_motion(nack_window *w, double x, double y)
{
    nack_wl_window *ww = nack__wl_win(w);
    nack_wl_decor_button hover;
    uint32_t edge;

    if (!ww)
        return;

    nack__wl.decor_x = x;
    nack__wl.decor_y = y;

    /* Show the cursor that says what a drag here would do. */
    edge = nack__wl_decor_resize_edge(w, nack__wl.decor_focus_part, x, y);
    nack__wl_apply_cursor_shape(edge ? nack__wl_cursor_for_edge(edge)
                                     : NACK_CURSOR_ARROW);

    hover = nack__wl_button_at(w, x, y);
    if (hover != ww->decor_hover) {
        ww->decor_hover = hover;
        nack__wl_decor_redraw(w);
    }
}

void nack__wl_decor_pointer_leave(nack_window *w)
{
    nack_wl_window *ww = nack__wl_win(w);
    if (ww && ww->decor_hover != NACK_WL_BUTTON_NONE) {
        ww->decor_hover = NACK_WL_BUTTON_NONE;
        nack__wl_decor_redraw(w);
    }
}

bool nack__wl_decor_pointer_button(nack_window *w, int button, bool down,
                                   uint32_t serial)
{
    nack_wl_window *ww = nack__wl_win(w);
    nack_wl_decor_button pressed;
    uint32_t edge;
    double x, y;

    if (!ww || !ww->client_side_decorations || !down)
        return false;

    x = nack__wl.decor_x;
    y = nack__wl.decor_y;

    if (button == NACK_MOUSE_RIGHT) {
        if (nack__wl.seat && ww->xdg_toplevel)
            xdg_toplevel_show_window_menu(ww->xdg_toplevel, nack__wl.seat, serial,
                                          (int32_t)x, (int32_t)y);
        return true;
    }

    if (button != NACK_MOUSE_LEFT)
        return false;

    pressed = nack__wl_button_at(w, x, y);
    switch (pressed) {
    case NACK_WL_BUTTON_CLOSE:
        w->should_close = true;
        w->emit_simple(NACK_WIN_EVENT_WINDOW_CLOSE);
        return true;
    case NACK_WL_BUTTON_MINIMIZE:
        xdg_toplevel_set_minimized(ww->xdg_toplevel);
        return true;
    case NACK_WL_BUTTON_MAXIMIZE:
        if (w->maximized)
            xdg_toplevel_unset_maximized(ww->xdg_toplevel);
        else
            xdg_toplevel_set_maximized(ww->xdg_toplevel);
        return true;
    default:
        break;
    }

    edge = nack__wl_decor_resize_edge(w, nack__wl.decor_focus_part, x, y);
    if (edge != 0) {
        xdg_toplevel_resize(ww->xdg_toplevel, nack__wl.seat, serial, edge);
        return true;
    }

    if (nack__wl.decor_focus_part == NACK_WL_DECOR_TOP) {
        /* Double-click on the title bar toggles maximise, as elsewhere. */
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
