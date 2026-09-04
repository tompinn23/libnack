/*
 * The public API: lifetime, frames, and translating window events into
 * cell-based ones.
 */
#include "nack_console_internal.h"
#include "nack_gfx.h"
#include "nack_guard.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

nack_console_state nack__c;

static void nack__log_to_stderr(const char *message, void *user_data)
{
    (void)user_data;
    fprintf(stderr, "%s\n", message);
}

bool nack_console_state::set_error(const char *fmt, ...)
{
    char message[512];
    va_list args;

    va_start(args, fmt);
    vsnprintf(message, sizeof message, fmt, args);
    va_end(args);

    /*
     * Recording the reason must not fail, or a report of a failed allocation
     * would be the thing that throws. The string keeps whatever it already
     * holds in that case, which is worse than the new reason but better than
     * losing the error entirely.
     */
    try {
        error = message;
    } catch (const std::exception &) {
    }
    has_error = true;
    return false;
}

void nack_console_state::clear_error()
{
    has_error = false;
    error.clear();
}

const char *nack_console_state::last_error() const
{
    return has_error ? error.c_str() : nullptr;
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                           */
/* ------------------------------------------------------------------ */

static void nack__sync_framebuffer(void)
{
    nack__c.fb_width = nack__c.window->fb_width;
    nack__c.fb_height = nack__c.window->fb_height;
    nack__render_update_viewport();
}

bool nack_console_state::init(const nack_config *config)
{
    nack_config cfg;
    nack_win_init_desc init_desc;
    nack_window_desc window_desc;
    int tile_w, tile_h, pixel_w, pixel_h, scale;

    if (initialized)
        return true;

    if (config)
        cfg = *config;
    if (cfg.columns < 1) cfg.columns = 80;
    if (cfg.rows < 1) cfg.rows = 50;
    if (!cfg.title) cfg.title = "libnack";

    *this = nack_console_state{};
    scaling = cfg.scaling;
    letterbox = cfg.letterbox;
    auto_resize = cfg.auto_resize;
    vsync = cfg.vsync;

    init_desc.app_id = cfg.title;
    /*
     * The public API has nowhere to hand a log callback, so the diagnostics -
     * which backend was picked, which renderer, why one of them was passed
     * over - are behind an environment variable instead. It is the only way a
     * user can tell us what happened on a machine we cannot reach.
     */
    if (getenv("NACK_DEBUG"))
        init_desc.log_fn = nack__log_to_stderr;
    if (!state.init(&init_desc)) {
        const char *message = nullptr;
        state.last_error(&message);
        return set_error("cannot open a window: %s",
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

    window = nack_window::create(&window_desc);
    if (!window) {
        const char *message = nullptr;
        state.last_error(&message);
        state.shutdown();
        return set_error("cannot create a window: %s",
                         message ? message : "unknown");
    }

    if (!nack__gfx_init(window)) {
        nack_window::destroy(window);
        window = nullptr;
        state.shutdown();
        return false;   /* the backend already described the failure */
    }
    nack__gfx_set_vsync(cfg.vsync);

    initialized = true;   /* tileset loading checks this */

    builtin_font = nack_tileset::builtin();
    if (!builtin_font) {
        shutdown();
        return false;
    }
    font = builtin_font;

    if (cfg.tileset) {
        nack_tileset *tileset =
            nack_tileset::load(cfg.tileset, cfg.tile_width, cfg.tile_height,
                               cfg.tileset_layout);
        if (!tileset) {
            shutdown();
            return false;   /* the loader already described the failure */
        }
        font = tileset;
    }

    root = nack_console::create(cfg.columns, cfg.rows);
    if (!root) {
        shutdown();
        return false;
    }

    /* Now the real tile size is known, size the window to match. */
    pixel_w = cfg.columns * font->tile_width;
    pixel_h = cfg.rows * font->tile_height;
    if (cfg.window_scale > 0) {
        pixel_w *= cfg.window_scale;
        pixel_h *= cfg.window_scale;
    }
    window->set_size(pixel_w, pixel_h);
    if (!cfg.auto_resize) {
        /* A fixed console should not be shrunk below one pixel per tile. */
        window->set_size_limits(cfg.columns * font->tile_width,
                                cfg.rows * font->tile_height, 0, 0);
    }

    if (cfg.fullscreen)
        window->set_fullscreen(true);

    window->show();
    nack__sync_framebuffer();

    start_time = nack__win_time_seconds();
    last_frame_time = start_time;
    has_error = false;
    return true;
}

void nack_console_state::shutdown()
{
    size_t i;

    /*
     * Tilesets hold textures, so they go before the renderer that owns them:
     * nack__gfx_texture_destroy needs a live backend to hand the texture back
     * to, and does nothing once there is not one. Shutting the renderer down
     * first leaked every atlas, the built-in font included.
     */
    for (i = tilesets.size(); i > 0; --i) {
        nack_tileset *tileset = tilesets[i - 1];
        if (tileset == builtin_font)
            continue;
        nack_tileset::destroy(tileset);
    }
    if (builtin_font) {
        nack_tileset *builtin = builtin_font;
        builtin_font = nullptr;   /* let the destroy go through */
        nack_tileset::destroy(builtin);
    }

    nack__gfx_shutdown();

    if (root) {
        nack_console *old_root = root;
        root = nullptr;
        nack_console::destroy(old_root);
    }

    if (window) {
        nack_window::destroy(window);
        state.shutdown();
    }

    /*
     * Assignment, not memset: the state holds a std::string and two vectors
     * now, and zeroing their bytes would drop what they point at rather than
     * release it.
     */
    *this = nack_console_state{};
}

/* ------------------------------------------------------------------ */
/* Frames                                                             */
/* ------------------------------------------------------------------ */

void nack_console_state::present()
{
    double now;

    if (!initialized)
        return;

    nack__gfx_begin_frame(letterbox, fb_width, fb_height, viewport_x,
                          viewport_y, viewport_w, viewport_h);
    nack__render_console(root);
    nack__gfx_end_frame();

    now = nack__win_time_seconds();
    delta = now - last_frame_time;
    last_frame_time = now;
}

void nack__debug_capture_frames(bool capture)
{
    nack__gfx_set_capture(capture);
}

bool nack__debug_read_pixel(int cell_x, int cell_y, uint8_t rgba[4])
{
    const nack_console *console = nack__c.root;
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

bool nack_console_state::should_close() const
{
    return close_requested || (window && window->should_close);
}

void nack_console_state::set_should_close(bool value)
{
    close_requested = value;
    if (window)
        window->set_should_close(value);
}

double nack_console_state::time() const
{
    if (!initialized)
        return 0.0;
    return nack__win_time_seconds() - start_time;
}

double nack_console_state::delta_time() const
{
    return delta;
}

/* ------------------------------------------------------------------ */
/* Events                                                             */
/* ------------------------------------------------------------------ */

/* Window pixels to console cells, accounting for the letterbox offset. */
static void nack__pixel_to_cell(double px, double py, int *cx, int *cy,
                                int *out_px, int *out_py)
{
    const nack_console *console = nack__c.root;
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
static bool nack__translate(const nack_win_event *in,
                            nack_event *out)
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
                nack__c.root->resize(columns, rows);
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

bool nack_console_state::poll_event(nack_event *event)
{
    nack_win_event raw;

    if (!initialized || !event)
        return false;
    while (state.poll_event(&raw)) {
        if (nack__translate(&raw, event))
            return true;
    }
    return false;
}

bool nack_console_state::wait_event(nack_event *event)
{
    return wait_event_timeout(event, -1.0);
}

bool nack_console_state::wait_event_timeout(nack_event *event,
                                            double seconds)
{
    nack_win_event raw;
    double deadline;

    if (!initialized || !event)
        return false;

    if (seconds < 0.0) {
        while (state.wait_event(&raw)) {
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
        if (!state.wait_event_timeout(&raw, remaining))
            return false;
        if (nack__translate(&raw, event))
            return true;
        if (nack__win_time_seconds() >= deadline)
            return false;
    }
}

void nack_console_state::wakeup()
{
    state.wakeup();
}

bool nack_console_state::key_down(nack_key key) const
{
    return state.key_is_down(key);
}

uint32_t nack_console_state::mods() const
{
    return state.get_mods();
}

bool nack_console_state::mouse_down(int button) const
{
    return state.mouse_button_is_down(button);
}

void nack_console_state::mouse_cell(int *x, int *y) const
{
    if (x) *x = mouse_cell_x;
    if (y) *y = mouse_cell_y;
}

const char *nack__key_name(nack_key key)
{
    return nack_key_get_name(key);
}

/* ------------------------------------------------------------------ */
/* Window                                                             */
/* ------------------------------------------------------------------ */

void nack_console_state::set_title(const char *title)
{
    if (window && title)
        window->set_title(title);
}

void nack_console_state::set_fullscreen(bool fullscreen)
{
    if (window)
        window->set_fullscreen(fullscreen);
}

bool nack_console_state::is_fullscreen() const
{
    return window && window->fullscreen;
}

void nack_console_state::set_vsync(bool on)
{
    vsync = on;
    nack__gfx_set_vsync(on);
}

void nack_console_state::set_font(nack_tileset *tileset)
{
    if (tileset)
        font = tileset;
}

/*
 * The window layer keeps its own error, and the two are not the same store:
 * a backend that explains itself through state.fail() is explaining it there,
 * where nack::app::last_error() never looks. Anything forwarded to that
 * layer has to carry the reason across, or the caller gets a bare false and
 * no account of why - which is what happened to every clipboard failure
 * until now.
 */
static bool nack__forward_error(const char *what)
{
    const char *message = nullptr;

    state.last_error(&message);
    return nack__c.set_error("%s: %s", what,
                             message && *message ? message
                                                  : "no reason given");
}

bool nack_console_state::clipboard_set(const char *utf8)
{
    if (!state.clipboard_set(utf8))
        return nack__forward_error("cannot set the clipboard");
    return true;
}

const char *nack_console_state::clipboard_get() const
{
    return state.clipboard_get();
}
