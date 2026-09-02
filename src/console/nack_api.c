/*
 * The public API: lifetime, frames, and translating window events into
 * cell-based ones.
 */
#include "nack_console_internal.h"
#include "nack_gfx.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nack_console_state nack__c;

static void nack__log_to_stderr(const char *message, void *user_data)
{
    (void)user_data;
    fprintf(stderr, "%s\n", message);
}

bool nack__error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(nack__c.error, sizeof nack__c.error, fmt, args);
    va_end(args);
    nack__c.has_error = true;
    return false;
}

void nack__clear_error(void)
{
    nack__c.has_error = false;
    nack__c.error[0] = '\0';
}

const char *nack_get_error(void)
{
    return nack__c.has_error ? nack__c.error : NULL;
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                           */
/* ------------------------------------------------------------------ */

void nack_config_defaults(struct nack_config *config)
{
    if (!config)
        return;
    memset(config, 0, sizeof *config);
    config->title = "libnack";
    config->columns = 80;
    config->rows = 50;
    config->tileset_layout = NACK_LAYOUT_CP437;
    config->scaling = NACK_SCALE_INTEGER;
    config->letterbox = NACK_BLACK;
    config->vsync = true;
    config->resizable = true;
    config->window_scale = 0;
}

static void nack__sync_framebuffer(void)
{
    nack_window_get_framebuffer_size(nack__c.window, &nack__c.fb_width,
                                          &nack__c.fb_height);
    nack__render_update_viewport();
}

bool nack_init(const struct nack_config *config)
{
    struct nack_config cfg;
    struct nack_win_init_desc init;
    struct nack_window_desc window_desc;
    int tile_w, tile_h, pixel_w, pixel_h, scale;

    if (nack__c.initialized)
        return true;

    nack_config_defaults(&cfg);
    if (config)
        cfg = *config;
    if (cfg.columns < 1) cfg.columns = 80;
    if (cfg.rows < 1) cfg.rows = 50;
    if (!cfg.title) cfg.title = "libnack";

    memset(&nack__c, 0, sizeof nack__c);
    nack__c.scaling = cfg.scaling;
    nack__c.letterbox = cfg.letterbox;
    nack__c.auto_resize = cfg.auto_resize;
    nack__c.vsync = cfg.vsync;

    memset(&init, 0, sizeof init);
    init.app_id = cfg.title;
    /*
     * The public API has nowhere to hand a log callback, so the diagnostics -
     * which backend was picked, which renderer, why one of them was passed
     * over - are behind an environment variable instead. It is the only way a
     * user can tell us what happened on a machine we cannot reach.
     */
    if (getenv("NACK_DEBUG"))
        init.log_fn = nack__log_to_stderr;
    if (!nack__win_init(&init)) {
        const char *message = NULL;
        nack__win_get_error(&message);
        return nack__error("cannot open a window: %s",
                           message ? message : "unknown");
    }

    /*
     * The window has to exist before a tileset can be uploaded, but its size
     * depends on the tile size, which is only known once the tileset is
     * loaded. Start from the built-in font's 8x8 and resize once the real
     * tileset is in.
     */
    tile_w = cfg.tile_width > 0 ? cfg.tile_width : 8;
    tile_h = cfg.tile_height > 0 ? cfg.tile_height : 8;
    scale = cfg.window_scale > 0 ? cfg.window_scale : 1;
    pixel_w = cfg.columns * tile_w * scale;
    pixel_h = cfg.rows * tile_h * scale;

    nack_window_desc_defaults(&window_desc);
    window_desc.title = cfg.title;
    window_desc.width = pixel_w;
    window_desc.height = pixel_h;
    window_desc.resizable = cfg.resizable;
    window_desc.visible = false;      /* shown once the first frame is ready */
    window_desc.framebuffer.red_bits = 8;
    window_desc.framebuffer.green_bits = 8;
    window_desc.framebuffer.blue_bits = 8;
    window_desc.framebuffer.alpha_bits = 0;
    window_desc.framebuffer.depth_bits = 0;
    window_desc.framebuffer.stencil_bits = 0;
    window_desc.framebuffer.double_buffer = true;

    nack__c.window = nack_window_create(&window_desc);
    if (!nack__c.window) {
        const char *message = NULL;
        nack__win_get_error(&message);
        nack__win_shutdown();
        return nack__error("cannot create a window: %s",
                           message ? message : "unknown");
    }

    if (!nack__gfx_init(nack__c.window)) {
        nack_window_destroy(nack__c.window);
        nack__c.window = NULL;
        nack__win_shutdown();
        return false;   /* the backend already described the failure */
    }
    nack__gfx_set_vsync(cfg.vsync);

    nack__c.initialized = true;   /* tileset loading checks this */

    nack__c.builtin_font = nack__tileset_builtin();
    if (!nack__c.builtin_font) {
        nack_shutdown();
        return false;
    }
    nack__c.font = nack__c.builtin_font;

    if (cfg.tileset) {
        struct nack_tileset *tileset =
            nack_tileset_load(cfg.tileset, cfg.tile_width, cfg.tile_height,
                              cfg.tileset_layout);
        if (!tileset) {
            nack_shutdown();
            return false;   /* the loader already described the failure */
        }
        nack__c.font = tileset;
    }

    nack__c.root = nack_console_new(cfg.columns, cfg.rows);
    if (!nack__c.root) {
        nack_shutdown();
        return false;
    }

    /* Now the real tile size is known, size the window to match. */
    pixel_w = cfg.columns * nack__c.font->tile_width;
    pixel_h = cfg.rows * nack__c.font->tile_height;
    if (cfg.window_scale > 0) {
        pixel_w *= cfg.window_scale;
        pixel_h *= cfg.window_scale;
    }
    nack_window_set_size(nack__c.window, pixel_w, pixel_h);
    if (!cfg.auto_resize) {
        /* A fixed console should not be shrunk below one pixel per tile. */
        nack_window_set_size_limits(nack__c.window,
                                         cfg.columns * nack__c.font->tile_width,
                                         cfg.rows * nack__c.font->tile_height,
                                         0, 0);
    }

    if (cfg.fullscreen)
        nack_window_set_fullscreen(nack__c.window, true);

    nack_window_show(nack__c.window);
    nack__sync_framebuffer();

    nack__c.start_time = nack__win_time_seconds();
    nack__c.last_frame_time = nack__c.start_time;
    nack__c.has_error = false;
    return true;
}

void nack_shutdown(void)
{
    size_t i;

    nack__gfx_shutdown();

    /* Copy first: freeing a tileset unregisters it and moves the array. */
    for (i = nack__c.tileset_count; i > 0; --i) {
        struct nack_tileset *tileset = nack__c.tilesets[i - 1];
        if (tileset == nack__c.builtin_font)
            continue;
        nack_tileset_free(tileset);
    }
    if (nack__c.builtin_font) {
        struct nack_tileset *builtin = nack__c.builtin_font;
        nack__c.builtin_font = NULL;   /* let the free go through */
        nack_tileset_free(builtin);
    }

    if (nack__c.root) {
        struct nack_console *root = nack__c.root;
        nack__c.root = NULL;
        nack_console_free(root);
    }

    free(nack__c.vertices);

    if (nack__c.window) {
        nack_window_destroy(nack__c.window);
        nack__win_shutdown();
    }

    memset(&nack__c, 0, sizeof nack__c);
}

/* ------------------------------------------------------------------ */
/* Frames                                                             */
/* ------------------------------------------------------------------ */

void nack_present(void)
{
    double now;

    if (!nack__c.initialized)
        return;

    nack__gfx_begin_frame(nack__c.letterbox, nack__c.fb_width, nack__c.fb_height,
                          nack__c.viewport_x, nack__c.viewport_y,
                          nack__c.viewport_w, nack__c.viewport_h);
    nack__render_console(nack__c.root);
    nack__gfx_end_frame();

    now = nack__win_time_seconds();
    nack__c.delta = now - nack__c.last_frame_time;
    nack__c.last_frame_time = now;
}

void nack__debug_capture_frames(bool capture)
{
    nack__gfx_set_capture(capture);
}

bool nack__debug_read_pixel(int cell_x, int cell_y, uint8_t rgba[4])
{
    const struct nack_console *console = nack__c.root;
    int px, py;

    if (!nack__c.initialized || !console)
        return false;

    /* Centre of the requested cell, in framebuffer coordinates. The GL origin
     * is bottom left, so the row is flipped. */
    px = nack__c.viewport_x +
         (int)(((double)cell_x + 0.5) * nack__c.viewport_w / console->columns);
    py = nack__c.viewport_y +
         (int)(((double)cell_y + 0.5) * nack__c.viewport_h / console->rows);
    if (px < 0 || py < 0 || px >= nack__c.fb_width || py >= nack__c.fb_height)
        return false;

    return nack__gfx_read_pixel(px, py, rgba);
}

struct nack_console *nack_root(void)
{
    return nack__c.root;
}

bool nack_should_close(void)
{
    return nack__c.should_close ||
           (nack__c.window && nack_window_should_close(nack__c.window));
}

void nack_set_should_close(bool value)
{
    nack__c.should_close = value;
    if (nack__c.window)
        nack_window_set_should_close(nack__c.window, value);
}

double nack_time(void)
{
    if (!nack__c.initialized)
        return 0.0;
    return nack__win_time_seconds() - nack__c.start_time;
}

double nack_delta_time(void)
{
    return nack__c.delta;
}

/* ------------------------------------------------------------------ */
/* Events                                                             */
/* ------------------------------------------------------------------ */

/* Window pixels to console cells, accounting for the letterbox offset. */
static void nack__pixel_to_cell(double px, double py, int *cx, int *cy,
                                int *out_px, int *out_py)
{
    const struct nack_console *console = nack__c.root;
    double scale = nack__c.dpi_scale > 0.0f ? nack__c.dpi_scale : 1.0;
    double fx = px * scale - nack__c.viewport_x;
    double fy = py * scale - nack__c.viewport_y;
    int columns = console ? console->columns : 1;
    int rows = console ? console->rows : 1;

    if (out_px) *out_px = (int)fx;
    if (out_py) *out_py = (int)fy;

    if (nack__c.viewport_w > 0 && nack__c.viewport_h > 0) {
        *cx = (int)(fx * columns / nack__c.viewport_w);
        *cy = (int)(fy * rows / nack__c.viewport_h);
    } else {
        *cx = *cy = 0;
    }
    if (*cx < 0) *cx = 0;
    if (*cy < 0) *cy = 0;
    if (*cx >= columns) *cx = columns - 1;
    if (*cy >= rows) *cy = rows - 1;
}

/* Returns false for window events the console does not surface. */
static bool nack__translate(const struct nack_win_event *in,
                            struct nack_event *out)
{
    memset(out, 0, sizeof *out);

    switch (in->type) {
    case NACK_WIN_EVENT_WINDOW_CLOSE:
    case NACK_WIN_EVENT_QUIT:
        out->type = NACK_EVENT_QUIT;
        return true;

    case NACK_WIN_EVENT_KEY_DOWN:
    case NACK_WIN_EVENT_KEY_UP:
        out->type = in->type == NACK_WIN_EVENT_KEY_DOWN ? NACK_EVENT_KEY_DOWN
                                                        : NACK_EVENT_KEY_UP;
        out->data.key.key = in->data.key.key;
        out->data.key.mods = in->data.key.mods;
        out->data.key.repeat = in->data.key.repeat;
        return true;

    case NACK_WIN_EVENT_TEXT:
        out->type = NACK_EVENT_TEXT;
        memcpy(out->data.text.utf8, in->data.text.utf8,
               sizeof out->data.text.utf8);
        return true;

    case NACK_WIN_EVENT_MOUSE_MOVE: {
        int cx, cy, px, py;
        nack__pixel_to_cell(in->data.motion.x, in->data.motion.y, &cx, &cy,
                            &px, &py);
        out->type = NACK_EVENT_MOUSE_MOVE;
        out->data.mouse.dx = cx - nack__c.mouse_cell_x;
        out->data.mouse.dy = cy - nack__c.mouse_cell_y;
        out->data.mouse.x = cx;
        out->data.mouse.y = cy;
        out->data.mouse.px = px;
        out->data.mouse.py = py;
        out->data.mouse.mods = in->data.motion.mods;
        out->data.mouse.button = -1;
        nack__c.mouse_cell_x = cx;
        nack__c.mouse_cell_y = cy;
        return true;
    }

    case NACK_WIN_EVENT_MOUSE_DOWN:
    case NACK_WIN_EVENT_MOUSE_UP: {
        int cx, cy, px, py;
        nack__pixel_to_cell(in->data.button.x, in->data.button.y, &cx, &cy,
                            &px, &py);
        out->type = in->type == NACK_WIN_EVENT_MOUSE_DOWN ? NACK_EVENT_MOUSE_DOWN
                                                          : NACK_EVENT_MOUSE_UP;
        out->data.mouse.x = cx;
        out->data.mouse.y = cy;
        out->data.mouse.px = px;
        out->data.mouse.py = py;
        out->data.mouse.button = in->data.button.button;
        out->data.mouse.clicks = in->data.button.click_count;
        out->data.mouse.mods = in->data.button.mods;
        nack__c.mouse_cell_x = cx;
        nack__c.mouse_cell_y = cy;
        return true;
    }

    case NACK_WIN_EVENT_MOUSE_SCROLL:
        out->type = NACK_EVENT_MOUSE_SCROLL;
        out->data.scroll.dx = in->data.scroll.dx;
        out->data.scroll.dy = in->data.scroll.dy;
        out->data.scroll.mods = in->data.scroll.mods;
        out->data.scroll.precise = in->data.scroll.precise;
        return true;

    case NACK_WIN_EVENT_WINDOW_FOCUS:
        out->type = NACK_EVENT_FOCUS;
        return true;
    case NACK_WIN_EVENT_WINDOW_BLUR:
        out->type = NACK_EVENT_BLUR;
        return true;

    case NACK_WIN_EVENT_WAKEUP:
        out->type = NACK_EVENT_WAKEUP;
        return true;

    case NACK_WIN_EVENT_WINDOW_RESIZE: {
        nack__c.fb_width = in->data.size.fb_width;
        nack__c.fb_height = in->data.size.fb_height;
        nack__gfx_resize(nack__c.fb_width, nack__c.fb_height);
        if (in->data.size.width > 0)
            nack__c.dpi_scale = (float)in->data.size.fb_width /
                                (float)in->data.size.width;

        if (nack__c.auto_resize && nack__c.root && nack__c.font) {
            int columns = nack__c.fb_width / nack__c.font->tile_width;
            int rows = nack__c.fb_height / nack__c.font->tile_height;
            if (columns < 1) columns = 1;
            if (rows < 1) rows = 1;
            if (columns != nack__c.root->columns || rows != nack__c.root->rows) {
                nack_console_resize(nack__c.root, columns, rows);
                nack__render_update_viewport();
                out->type = NACK_EVENT_RESIZE;
                out->data.resize.columns = columns;
                out->data.resize.rows = rows;
                return true;
            }
        }
        nack__render_update_viewport();
        return false;   /* a fixed console just re-letterboxes */
    }

    default:
        return false;   /* expose, move, minimise and friends stay internal */
    }
}

bool nack_poll_event(struct nack_event *event)
{
    struct nack_win_event raw;

    if (!nack__c.initialized || !event)
        return false;
    while (nack__win_poll_event(&raw)) {
        if (nack__translate(&raw, event))
            return true;
    }
    return false;
}

bool nack_wait_event(struct nack_event *event)
{
    return nack_wait_event_timeout(event, -1.0);
}

bool nack_wait_event_timeout(struct nack_event *event, double seconds)
{
    struct nack_win_event raw;
    double deadline;

    if (!nack__c.initialized || !event)
        return false;

    if (seconds < 0.0) {
        while (nack__win_wait_event(&raw)) {
            if (nack__translate(&raw, event))
                return true;
        }
        return false;
    }

    /* Window events the console hides must not shorten the caller's wait. */
    deadline = nack__win_time_seconds() + seconds;
    for (;;) {
        double remaining = deadline - nack__win_time_seconds();
        if (remaining < 0.0)
            remaining = 0.0;
        if (!nack__win_wait_event_timeout(&raw, remaining))
            return false;
        if (nack__translate(&raw, event))
            return true;
        if (nack__win_time_seconds() >= deadline)
            return false;
    }
}

void nack_wakeup(void)
{
    nack__win_wakeup();
}

bool nack_key_down(enum nack_key key)
{
    return nack__win_key_is_down(key);
}

uint32_t nack_mods(void)
{
    return nack__win_get_mods();
}

bool nack_mouse_down(int button)
{
    return nack__win_mouse_button_is_down(button);
}

void nack_mouse_cell(int *x, int *y)
{
    if (x) *x = nack__c.mouse_cell_x;
    if (y) *y = nack__c.mouse_cell_y;
}

const char *nack_key_name(enum nack_key key)
{
    return nack_key_get_name(key);
}

/* ------------------------------------------------------------------ */
/* Window                                                             */
/* ------------------------------------------------------------------ */

void nack_set_title(const char *title)
{
    if (nack__c.window && title)
        nack_window_set_title(nack__c.window, title);
}

void nack_set_fullscreen(bool fullscreen)
{
    if (nack__c.window)
        nack_window_set_fullscreen(nack__c.window, fullscreen);
}

bool nack_is_fullscreen(void)
{
    return nack__c.window && nack_window_is_fullscreen(nack__c.window);
}

void nack_set_vsync(bool vsync)
{
    nack__c.vsync = vsync;
    nack__gfx_set_vsync(vsync);
}

bool nack_clipboard_set(const char *utf8)
{
    return nack__win_clipboard_set(utf8);
}

const char *nack_clipboard_get(void)
{
    return nack__win_clipboard_get();
}
