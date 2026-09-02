/* libnack - platform independent core: dispatch, event queue, window shell. */
#include "nack_internal.h"

#include <stdio.h>

#if defined(NACK_PLATFORM_WIN32)
#  if defined(NACK_WIN32_USE_SDK_HEADERS)
#    include <windows.h>
#  else
#    include "win32/nack_win32_api.h"
#  endif
#else
#  include <time.h>
#endif

struct nack_state nack__g;

#define NACK_DOUBLE_CLICK_NS      400000000ull   /* 400 ms */
#define NACK_DOUBLE_CLICK_SLOP    4.0            /* logical pixels */

/* ------------------------------------------------------------------ */
/* Diagnostics                                                        */
/* ------------------------------------------------------------------ */

void nack__log(const char *fmt, ...)
{
    if (!nack__g.log_fn)
        return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    nack__g.log_fn(buf, nack__g.log_user_data);
}

bool nack__fail(enum nack_result code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(nack__g.error_message, sizeof nack__g.error_message, fmt, ap);
    va_end(ap);
    nack__g.error_code = code;
    nack__log("nack: error: %s", nack__g.error_message);
    return false;
}

enum nack_result nack__win_get_error(const char **message)
{
    if (message)
        *message = nack__g.error_message;
    return nack__g.error_code;
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

char *nack__strdup(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

void *nack__calloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p)
        nack__fail(NACK_ERROR_OUT_OF_MEMORY, "out of memory");
    return p;
}

uint32_t nack__utf8_encode(uint32_t cp, char out[5])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        out[1] = 0;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        out[2] = 0;
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = 0;
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    out[4] = 0;
    return 4;
}

/* Filters out control characters that arrive as "text" on some platforms;
 * a terminal wants those as key events, not as pasted glyphs. */
bool nack__codepoint_is_text(uint32_t cp)
{
    if (cp < 0x20 || cp == 0x7F)
        return false;
    if (cp >= 0x80 && cp <= 0x9F)
        return false;
    if (cp > 0x10FFFF)
        return false;
    if (cp >= 0xD800 && cp <= 0xDFFF)
        return false;
    /* Unicode private-use area used by macOS for function keys. */
    if (cp >= 0xF700 && cp <= 0xF8FF)
        return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Time                                                               */
/* ------------------------------------------------------------------ */

uint64_t nack__win_time_ns(void)
{
#if defined(NACK_PLATFORM_WIN32)
    static int64_t frequency;
    int64_t now;
    /*
     * The real SDK declares these as taking LARGE_INTEGER*, a union; the
     * hand-rolled header declares the int64_t the union actually holds, since
     * that is the only member anything here reads. C converted between the
     * two silently and C++ does not, so the SDK path goes through the union.
     */
#  if defined(NACK_WIN32_USE_SDK_HEADERS)
    if (frequency == 0) {
        LARGE_INTEGER ticks;
        QueryPerformanceFrequency(&ticks);
        frequency = ticks.QuadPart;
    }
    {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        now = ticks.QuadPart;
    }
#  else
    if (frequency == 0)
        QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
#  endif
    return (uint64_t)(((double)now / (double)frequency) * 1e9);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

double nack__win_time_seconds(void)
{
    return (double)nack__win_time_ns() / 1e9;
}

/* ------------------------------------------------------------------ */
/* Event queue                                                        */
/* ------------------------------------------------------------------ */

bool nack__queue_empty(void)
{
    return nack__g.queue_head == nack__g.queue_tail;
}

void nack__push_event(const struct nack_win_event *ev)
{
    size_t next = (nack__g.queue_tail + 1u) % NACK_WIN_EVENT_QUEUE_CAP;
    if (next == nack__g.queue_head) {
        /* Queue full: drop the oldest event so input stays responsive
         * rather than the newest, which is usually the interesting one. */
        nack__g.queue_head = (nack__g.queue_head + 1u) % NACK_WIN_EVENT_QUEUE_CAP;
    }
    nack__g.queue[nack__g.queue_tail] = *ev;
    nack__g.queue_tail = next;
}

struct nack_win_event *nack__event_begin(enum nack_win_event_type type, struct nack_window *w)
{
    static struct nack_win_event scratch;
    memset(&scratch, 0, sizeof scratch);
    scratch.type = type;
    scratch.window = w;
    scratch.time_ns = nack__win_time_ns();
    return &scratch;
}

static bool nack__pop(struct nack_win_event *out)
{
    if (nack__queue_empty())
        return false;
    *out = nack__g.queue[nack__g.queue_head];
    nack__g.queue_head = (nack__g.queue_head + 1u) % NACK_WIN_EVENT_QUEUE_CAP;
    return true;
}

/* ------------------------------------------------------------------ */
/* Event emitters                                                     */
/* ------------------------------------------------------------------ */

void nack__emit_simple(struct nack_window *w, enum nack_win_event_type type)
{
    struct nack_win_event *ev = nack__event_begin(type, w);
    nack__push_event(ev);
}

void nack__emit_resize(struct nack_window *w, int width, int height, int fb_width,
                       int fb_height)
{
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (fb_width < 1) fb_width = width;
    if (fb_height < 1) fb_height = height;

    if (w->width == width && w->height == height &&
        w->fb_width == fb_width && w->fb_height == fb_height)
        return;

    w->width = width;
    w->height = height;
    w->fb_width = fb_width;
    w->fb_height = fb_height;

    struct nack_win_event *ev = nack__event_begin(NACK_WIN_EVENT_WINDOW_RESIZE, w);
    ev->data.size.width = width;
    ev->data.size.height = height;
    ev->data.size.fb_width = fb_width;
    ev->data.size.fb_height = fb_height;
    nack__push_event(ev);
}

void nack__emit_scale(struct nack_window *w, float scale)
{
    if (scale <= 0.0f || w->scale == scale)
        return;
    w->scale = scale;
    struct nack_win_event *ev = nack__event_begin(NACK_WIN_EVENT_WINDOW_SCALE, w);
    ev->data.scale.scale = scale;
    nack__push_event(ev);
}

void nack__emit_focus(struct nack_window *w, bool focused)
{
    if (w->focused == focused)
        return;
    w->focused = focused;
    if (!focused) {
        /* Never leave keys latched when focus leaves the window. */
        for (int i = 0; i < NACK_KEY_COUNT; ++i) {
            if (nack__g.keys[i]) {
                nack__g.keys[i] = false;
                nack__emit_key(w, (enum nack_key)i, 0, nack__g.mods, false, false);
            }
        }
        nack__g.mods = 0;
    }
    nack__emit_simple(w, focused ? NACK_WIN_EVENT_WINDOW_FOCUS : NACK_WIN_EVENT_WINDOW_BLUR);
}

void nack__emit_key(struct nack_window *w, enum nack_key key, uint32_t scancode, uint32_t mods,
                    bool down, bool repeat)
{
    if (key > 0 && key < NACK_KEY_COUNT) {
        if (!down && !nack__g.keys[key] && !repeat) {
            /* Release without a matching press (focus loss, grab). Still
             * report it: consumers track their own state. */
        }
        nack__g.keys[key] = down;
    }
    nack__g.mods = mods;

    struct nack_win_event *ev = nack__event_begin(down ? NACK_WIN_EVENT_KEY_DOWN : NACK_WIN_EVENT_KEY_UP, w);
    ev->data.key.key = key;
    ev->data.key.scancode = scancode;
    ev->data.key.mods = mods;
    ev->data.key.repeat = repeat;
    nack__push_event(ev);
}

void nack__emit_text(struct nack_window *w, const char *utf8)
{
    if (!utf8 || !utf8[0])
        return;
    struct nack_win_event *ev = nack__event_begin(NACK_WIN_EVENT_TEXT, w);
    size_t n = strlen(utf8);
    if (n >= sizeof ev->data.text.utf8)
        n = sizeof ev->data.text.utf8 - 1;
    memcpy(ev->data.text.utf8, utf8, n);
    ev->data.text.utf8[n] = '\0';
    nack__push_event(ev);
}

void nack__emit_mouse_move(struct nack_window *w, double x, double y, uint32_t mods)
{
    struct nack_win_event *ev = nack__event_begin(NACK_WIN_EVENT_MOUSE_MOVE, w);
    ev->data.motion.dx = x - w->mouse_x;
    ev->data.motion.dy = y - w->mouse_y;
    ev->data.motion.x = x;
    ev->data.motion.y = y;
    ev->data.motion.mods = mods;
    w->mouse_x = x;
    w->mouse_y = y;
    nack__g.mods = mods;
    nack__push_event(ev);
}

void nack__emit_mouse_button(struct nack_window *w, int button, bool down, double x, double y,
                             uint32_t mods)
{
    if (button < 0 || button >= NACK_MOUSE_BUTTON_COUNT)
        return;
    nack__g.mouse_buttons[button] = down;
    nack__g.mods = mods;
    w->mouse_x = x;
    w->mouse_y = y;

    int clicks = 1;
    if (down) {
        uint64_t now = nack__win_time_ns();
        double ddx = x - w->last_click_x;
        double ddy = y - w->last_click_y;
        /* Not named `near`: windows.h still defines that as a macro. */
        bool within_slop = (ddx * ddx + ddy * ddy) <=
                           (NACK_DOUBLE_CLICK_SLOP * NACK_DOUBLE_CLICK_SLOP);
        if (button == w->last_click_button && within_slop &&
            now - w->last_click_time_ns <= NACK_DOUBLE_CLICK_NS) {
            w->click_count++;
        } else {
            w->click_count = 1;
        }
        w->last_click_time_ns = now;
        w->last_click_x = x;
        w->last_click_y = y;
        w->last_click_button = button;
        clicks = w->click_count;
    } else {
        clicks = w->click_count ? w->click_count : 1;
    }

    struct nack_win_event *ev = nack__event_begin(down ? NACK_WIN_EVENT_MOUSE_DOWN : NACK_WIN_EVENT_MOUSE_UP, w);
    ev->data.button.button = button;
    ev->data.button.x = x;
    ev->data.button.y = y;
    ev->data.button.mods = mods;
    ev->data.button.click_count = clicks;
    nack__push_event(ev);
}

void nack__emit_scroll(struct nack_window *w, double dx, double dy, uint32_t mods,
                       bool precise)
{
    if (dx == 0.0 && dy == 0.0)
        return;
    struct nack_win_event *ev = nack__event_begin(NACK_WIN_EVENT_MOUSE_SCROLL, w);
    ev->data.scroll.dx = dx;
    ev->data.scroll.dy = dy;
    ev->data.scroll.mods = mods;
    ev->data.scroll.precise = precise;
    nack__push_event(ev);
}

/* ------------------------------------------------------------------ */
/* Window registry                                                    */
/* ------------------------------------------------------------------ */

void nack__register_window(struct nack_window *w)
{
    if (nack__g.window_count < NACK_MAX_WINDOWS)
        nack__g.windows[nack__g.window_count++] = w;
}

void nack__unregister_window(struct nack_window *w)
{
    for (size_t i = 0; i < nack__g.window_count; ++i) {
        if (nack__g.windows[i] == w) {
            nack__g.windows[i] = nack__g.windows[--nack__g.window_count];
            nack__g.windows[nack__g.window_count] = NULL;
            return;
        }
    }
}

/* Drop queued events referring to a window that is going away. */
static void nack__purge_window_events(struct nack_window *w)
{
    size_t read = nack__g.queue_head;
    size_t write = nack__g.queue_head;
    while (read != nack__g.queue_tail) {
        if (nack__g.queue[read].window != w) {
            if (write != read)
                nack__g.queue[write] = nack__g.queue[read];
            write = (write + 1u) % NACK_WIN_EVENT_QUEUE_CAP;
        }
        read = (read + 1u) % NACK_WIN_EVENT_QUEUE_CAP;
    }
    nack__g.queue_tail = write;
}

/* ------------------------------------------------------------------ */
/* Backend selection                                                  */
/* ------------------------------------------------------------------ */

const char *nack__win_backend_name(enum nack_backend backend)
{
    switch (backend) {
    case NACK_BACKEND_WIN32:   return "win32";
    case NACK_BACKEND_COCOA:   return "cocoa";
    case NACK_BACKEND_WAYLAND: return "wayland";
    case NACK_BACKEND_X11:     return "x11";
    default:                   return "none";
    }
}

static const struct nack_backend_vt *nack__select_backend(enum nack_backend preferred)
{
#if defined(NACK_PLATFORM_WIN32)
    (void)preferred;
    return nack__backend_win32();
#elif defined(NACK_PLATFORM_COCOA)
    (void)preferred;
    return nack__backend_cocoa();
#elif defined(NACK_HAS_WAYLAND) && defined(NACK_HAS_X11)
    const char *env = getenv("NACK_BACKEND");
    if (env && *env) {
        if (strcmp(env, "wayland") == 0)
            preferred = NACK_BACKEND_WAYLAND;
        else if (strcmp(env, "x11") == 0 || strcmp(env, "xcb") == 0)
            preferred = NACK_BACKEND_X11;
    }

    if (preferred == NACK_BACKEND_X11)
        return nack__backend_x11();
    if (preferred == NACK_BACKEND_WAYLAND)
        return nack__backend_wayland();

    /* Auto: a compositor socket in the environment means a Wayland session,
     * even when an Xwayland DISPLAY is also present. */
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display && *wayland_display)
        return nack__backend_wayland();
    return nack__backend_x11();
#elif defined(NACK_HAS_WAYLAND)
    (void)preferred;
    return nack__backend_wayland();
#elif defined(NACK_HAS_X11)
    (void)preferred;
    return nack__backend_x11();
#else
    (void)preferred;
    return NULL;
#endif
}

/* Try each available Unix backend in order until one initializes. */
static bool nack__try_init_backends(const struct nack_win_init_desc *desc)
{
#if defined(NACK_PLATFORM_UNIX)
    const struct nack_backend_vt *order[2] = { NULL, NULL };
    size_t n = 0;
    const struct nack_backend_vt *first = nack__select_backend(desc->backend);
    if (!first)
        return nack__fail(NACK_ERROR_NO_BACKEND, "no display backend compiled in");
    order[n++] = first;
#if defined(NACK_HAS_WAYLAND) && defined(NACK_HAS_X11)
    order[n++] = (first->id == NACK_BACKEND_WAYLAND) ? nack__backend_x11()
                                                     : nack__backend_wayland();
#endif
    for (size_t i = 0; i < n; ++i) {
        if (!order[i])
            continue;
        nack__g.vt = order[i];
        if (order[i]->init(desc))
            return true;
        nack__log("nack: backend '%s' unavailable: %s", order[i]->name,
                  nack__g.error_message);
        nack__g.vt = NULL;
    }
    return nack__fail(NACK_ERROR_NO_BACKEND,
                      "no usable display backend (tried wayland/x11)");
#else
    const struct nack_backend_vt *vt = nack__select_backend(desc->backend);
    if (!vt)
        return nack__fail(NACK_ERROR_NO_BACKEND, "no display backend compiled in");
    nack__g.vt = vt;
    if (vt->init(desc))
        return true;
    nack__g.vt = NULL;
    return false;
#endif
}

bool nack__win_init(const struct nack_win_init_desc *desc)
{
    if (nack__g.initialized)
        return true;

    struct nack_win_init_desc local;
    memset(&local, 0, sizeof local);
    if (desc)
        local = *desc;

    memset(&nack__g, 0, sizeof nack__g);
    nack__g.log_fn = local.log_fn;
    nack__g.log_user_data = local.log_user_data;
    nack__g.app_id = nack__strdup(local.app_id ? local.app_id : "libnack");
    nack__g.swap_interval = 1;

    if (!nack__try_init_backends(&local)) {
        free(nack__g.app_id);
        nack__g.app_id = NULL;
        return false;
    }

    nack__g.initialized = true;
    nack__log("nack: using %s backend", nack__g.vt->name);
    return true;
}

void nack__win_shutdown(void)
{
    if (!nack__g.initialized)
        return;

    while (nack__g.window_count > 0)
        nack_window_destroy(nack__g.windows[nack__g.window_count - 1]);

    if (nack__g.vt && nack__g.vt->shutdown)
        nack__g.vt->shutdown();

    /* Entry points belong to the driver we are dropping. */
    nack__proc_cache_clear();

    free(nack__g.app_id);
    memset(&nack__g, 0, sizeof nack__g);
}

bool nack__win_is_initialized(void)
{
    return nack__g.initialized;
}

enum nack_backend nack__win_get_backend(void)
{
    return nack__g.vt ? nack__g.vt->id : NACK_BACKEND_NONE;
}

/* ------------------------------------------------------------------ */
/* Descriptor defaults                                                */
/* ------------------------------------------------------------------ */

void nack_window_desc_defaults(struct nack_window_desc *desc)
{
    if (!desc)
        return;
    memset(desc, 0, sizeof *desc);
    desc->title = "libnack";
    desc->width = 800;
    desc->height = 600;
    desc->resizable = true;
    desc->decorated = true;
    desc->visible = true;
    desc->high_dpi = true;
    nack_framebuffer_desc_defaults(&desc->framebuffer);
}

void nack__gl_desc_defaults(struct nack__gl_desc *desc)
{
    if (!desc)
        return;
    memset(desc, 0, sizeof *desc);
    desc->major = 3;
    desc->minor = 3;
    desc->profile = NACK__GL_PROFILE_CORE;
}

void nack_framebuffer_desc_defaults(struct nack_framebuffer_desc *desc)
{
    if (!desc)
        return;
    memset(desc, 0, sizeof *desc);
    desc->red_bits = 8;
    desc->green_bits = 8;
    desc->blue_bits = 8;
    desc->alpha_bits = 0;
    desc->depth_bits = 24;
    desc->stencil_bits = 8;
    desc->double_buffer = true;
}

/* ------------------------------------------------------------------ */
/* Windows                                                            */
/* ------------------------------------------------------------------ */

#define NACK_REQUIRE_INIT_RET(ret)                                            \
    do {                                                                      \
        if (!nack__g.initialized) {                                           \
            nack__fail(NACK_ERROR_NOT_INITIALIZED, "nack__win_init() not called"); \
            return ret;                                                       \
        }                                                                     \
    } while (0)

#define NACK_REQUIRE_INIT() NACK_REQUIRE_INIT_RET()

struct nack_window *nack_window_create(const struct nack_window_desc *desc)
{
    NACK_REQUIRE_INIT_RET(NULL);

    struct nack_window_desc d;
    nack_window_desc_defaults(&d);
    if (desc) {
        d = *desc;
        if (!d.title) d.title = "libnack";
        if (d.width <= 0) d.width = 800;
        if (d.height <= 0) d.height = 600;
        /* A caller that memset its descriptor gets a usable format rather
         * than a request for a 0-bit framebuffer. */
        if (d.framebuffer.red_bits == 0 && d.framebuffer.green_bits == 0 &&
            d.framebuffer.blue_bits == 0 && d.framebuffer.depth_bits == 0)
            nack_framebuffer_desc_defaults(&d.framebuffer);
    }
    if (d.transparent && d.framebuffer.alpha_bits == 0)
        d.framebuffer.alpha_bits = 8;

    struct nack_window *w = (struct nack_window *)nack__calloc(1, sizeof *w);
    if (!w)
        return NULL;

    w->vt = nack__g.vt;
    w->title = nack__strdup(d.title);
    w->width = d.width;
    w->height = d.height;
    w->fb_width = d.width;
    w->fb_height = d.height;
    w->scale = 1.0f;
    w->min_width = d.min_width;
    w->min_height = d.min_height;
    w->max_width = d.max_width;
    w->max_height = d.max_height;
    w->inc_width = d.width_increment;
    w->inc_height = d.height_increment;
    w->resizable = d.resizable;
    w->decorated = d.decorated;
    w->transparent = d.transparent;
    w->high_dpi = d.high_dpi;
    w->framebuffer = d.framebuffer;
    w->user_data = d.user_data;
    w->cursor_mode = NACK_CURSOR_MODE_NORMAL;
    w->cursor_shape = NACK_CURSOR_ARROW;
    w->last_click_button = -1;

    if (!nack__g.vt->window_create(w, &d)) {
        free(w->title);
        free(w);
        return NULL;
    }

    nack__register_window(w);

    if (d.fullscreen)
        nack_window_set_fullscreen(w, true);
    else if (d.maximized)
        nack_window_maximize(w);

    if (d.visible)
        nack_window_show(w);

    return w;
}

void nack_window_destroy(struct nack_window *window)
{
    if (!window || window->destroyed)
        return;
    window->destroyed = true;

    if (nack__g.current_window == window) {
        nack__g.current_window = NULL;
        nack__g.current_context = NULL;
    }

    nack__purge_window_events(window);
    nack__unregister_window(window);
    window->vt->window_destroy(window);
    free(window->title);
    free(window);
}

void nack_window_show(struct nack_window *window)
{
    if (!window) return;
    window->vt->window_show(window, true);
    window->visible = true;
}

void nack_window_hide(struct nack_window *window)
{
    if (!window) return;
    window->vt->window_show(window, false);
    window->visible = false;
}

void nack_window_focus(struct nack_window *window)
{
    if (window && window->vt->window_focus)
        window->vt->window_focus(window);
}

void nack_window_minimize(struct nack_window *window)
{
    if (window && window->vt->window_minimize)
        window->vt->window_minimize(window);
}

void nack_window_maximize(struct nack_window *window)
{
    if (window && window->vt->window_maximize)
        window->vt->window_maximize(window);
}

void nack_window_restore(struct nack_window *window)
{
    if (window && window->vt->window_restore)
        window->vt->window_restore(window);
}

void nack_window_request_attention(struct nack_window *window)
{
    if (window && window->vt->window_request_attention)
        window->vt->window_request_attention(window);
}

void nack_window_set_title(struct nack_window *window, const char *title)
{
    if (!window || !title)
        return;
    char *copy = nack__strdup(title);
    if (!copy)
        return;
    free(window->title);
    window->title = copy;
    window->vt->window_set_title(window, title);
}

void nack_window_set_size(struct nack_window *window, int width, int height)
{
    if (!window || width < 1 || height < 1)
        return;
    window->vt->window_set_size(window, width, height);
}

void nack_window_get_size(const struct nack_window *window, int *width, int *height)
{
    if (width)  *width  = window ? window->width : 0;
    if (height) *height = window ? window->height : 0;
}

void nack_window_get_framebuffer_size(const struct nack_window *window, int *width,
                                      int *height)
{
    if (width)  *width  = window ? window->fb_width : 0;
    if (height) *height = window ? window->fb_height : 0;
}

void nack_window_get_position(const struct nack_window *window, int *x, int *y)
{
    if (x) *x = window ? window->pos_x : 0;
    if (y) *y = window ? window->pos_y : 0;
}

void nack_window_set_position(struct nack_window *window, int x, int y)
{
    if (window && window->vt->window_set_position)
        window->vt->window_set_position(window, x, y);
}

float nack_window_get_content_scale(const struct nack_window *window)
{
    return window ? window->scale : 1.0f;
}

void nack_window_set_size_limits(struct nack_window *window, int min_width, int min_height,
                                 int max_width, int max_height)
{
    if (!window) return;
    window->min_width = min_width > 0 ? min_width : 0;
    window->min_height = min_height > 0 ? min_height : 0;
    window->max_width = max_width > 0 ? max_width : 0;
    window->max_height = max_height > 0 ? max_height : 0;
    if (window->vt->window_apply_size_hints)
        window->vt->window_apply_size_hints(window);
}

void nack_window_set_size_increments(struct nack_window *window, int dw, int dh)
{
    if (!window) return;
    window->inc_width = dw > 0 ? dw : 0;
    window->inc_height = dh > 0 ? dh : 0;
    if (window->vt->window_apply_size_hints)
        window->vt->window_apply_size_hints(window);
}

void nack_window_set_fullscreen(struct nack_window *window, bool fullscreen)
{
    if (window && window->vt->window_set_fullscreen)
        window->vt->window_set_fullscreen(window, fullscreen);
}

bool nack_window_is_fullscreen(const struct nack_window *window)  { return window && window->fullscreen; }
bool nack_window_is_focused(const struct nack_window *window)     { return window && window->focused; }
bool nack_window_is_minimized(const struct nack_window *window)   { return window && window->minimized; }
bool nack_window_is_maximized(const struct nack_window *window)   { return window && window->maximized; }
bool nack_window_should_close(const struct nack_window *window)   { return window && window->should_close; }

void nack_window_set_should_close(struct nack_window *window, bool value)
{
    if (window)
        window->should_close = value;
}

void *nack_window_get_user_data(const struct nack_window *window)
{
    return window ? window->user_data : NULL;
}

void nack_window_set_user_data(struct nack_window *window, void *user_data)
{
    if (window)
        window->user_data = user_data;
}

void nack_window_set_cursor_shape(struct nack_window *window,
                                  enum nack_cursor_shape shape)
{
    if (!window || shape < 0 || shape >= NACK_CURSOR_SHAPE_COUNT)
        return;
    window->cursor_shape = shape;
    if (window->vt->window_set_cursor_shape)
        window->vt->window_set_cursor_shape(window, shape);
}

void nack_window_set_cursor_mode(struct nack_window *window, enum nack_cursor_mode mode)
{
    if (!window)
        return;
    window->cursor_mode = mode;
    if (window->vt->window_set_cursor_mode)
        window->vt->window_set_cursor_mode(window, mode);
}

enum nack_cursor_mode nack_window_get_cursor_mode(const struct nack_window *window)
{
    return window ? window->cursor_mode : NACK_CURSOR_MODE_NORMAL;
}

void nack_window_request_redraw(struct nack_window *window)
{
    if (!window)
        return;
    if (window->vt->window_request_redraw)
        window->vt->window_request_redraw(window);
    else
        nack__emit_simple(window, NACK_WIN_EVENT_WINDOW_EXPOSE);
}

void nack_window_get_native(const struct nack_window *window,
                            struct nack_native_window *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof *out);
    if (!window)
        return;
    out->backend = window->vt->id;
    if (window->vt->window_get_native)
        window->vt->window_get_native(window, out);
}

/* ------------------------------------------------------------------ */
/* Event loop                                                         */
/* ------------------------------------------------------------------ */

bool nack__win_poll_event(struct nack_win_event *event)
{
    NACK_REQUIRE_INIT_RET(false);
    if (!event)
        return false;
    if (nack__pop(event))
        return true;
    nack__g.vt->pump_events(0.0);
    return nack__pop(event);
}

bool nack__win_wait_event(struct nack_win_event *event)
{
    return nack__win_wait_event_timeout(event, -1.0);
}

bool nack__win_wait_event_timeout(struct nack_win_event *event, double timeout)
{
    NACK_REQUIRE_INIT_RET(false);
    if (!event)
        return false;
    if (nack__pop(event))
        return true;

    if (timeout < 0.0) {
        /* Block until the platform produces something we can hand back. */
        while (nack__queue_empty()) {
            nack__g.vt->pump_events(-1.0);
            if (nack__g.window_count == 0 && nack__queue_empty())
                return false;   /* nothing left that could wake us */
        }
        return nack__pop(event);
    }

    double deadline = nack__win_time_seconds() + timeout;
    for (;;) {
        double remaining = deadline - nack__win_time_seconds();
        if (remaining < 0.0)
            remaining = 0.0;
        nack__g.vt->pump_events(remaining);
        if (nack__pop(event))
            return true;
        if (nack__win_time_seconds() >= deadline)
            return false;
    }
}

void nack__win_wakeup(void)
{
    if (nack__g.initialized && nack__g.vt->wakeup)
        nack__g.vt->wakeup();
}

bool nack__win_key_is_down(enum nack_key key)
{
    if (key <= 0 || key >= NACK_KEY_COUNT)
        return false;
    return nack__g.keys[key];
}

uint32_t nack__win_get_mods(void)
{
    return nack__g.mods;
}

bool nack__win_mouse_button_is_down(int button)
{
    if (button < 0 || button >= NACK_MOUSE_BUTTON_COUNT)
        return false;
    return nack__g.mouse_buttons[button];
}

void nack__win_get_mouse_position(struct nack_window *window, double *x, double *y)
{
    if (x) *x = window ? window->mouse_x : 0.0;
    if (y) *y = window ? window->mouse_y : 0.0;
}

/* ------------------------------------------------------------------ */
/* OpenGL                                                             */
/* ------------------------------------------------------------------ */

struct nack_gl_context *nack__gl_context_create(struct nack_window *window,
                                               const struct nack__gl_desc *desc)
{
    NACK_REQUIRE_INIT_RET(NULL);
    if (!window) {
        nack__fail(NACK_ERROR_INVALID_ARGUMENT, "nack__gl_context_create: null window");
        return NULL;
    }
    struct nack__gl_desc d;
    nack__gl_desc_defaults(&d);
    if (desc)
        d = *desc;
    if (!nack__g.vt->gl_create) {
        nack__fail(NACK_ERROR_UNSUPPORTED, "backend has no OpenGL support");
        return NULL;
    }
    return nack__g.vt->gl_create(window, &d);
}

void nack__gl_context_destroy(struct nack_gl_context *context)
{
    if (!context)
        return;
    if (nack__g.current_context == context) {
        nack__g.current_context = NULL;
        nack__g.current_window = NULL;
    }
    context->vt->gl_destroy(context);
}

bool nack__gl_make_current(struct nack_window *window, struct nack_gl_context *context)
{
    NACK_REQUIRE_INIT_RET(false);
    if (!nack__g.vt->gl_make_current)
        return nack__fail(NACK_ERROR_UNSUPPORTED, "backend has no OpenGL support");
    if (!nack__g.vt->gl_make_current(window, context))
        return false;
    nack__g.current_window = context ? window : NULL;
    nack__g.current_context = context;
    return true;
}

void nack__gl_swap_buffers(struct nack_window *window)
{
    if (window && nack__g.vt && nack__g.vt->gl_swap_buffers)
        nack__g.vt->gl_swap_buffers(window);
}

void nack__gl_set_swap_interval(int interval)
{
    if (!nack__g.initialized)
        return;
    nack__g.swap_interval = interval;
    if (nack__g.vt->gl_set_swap_interval)
        nack__g.vt->gl_set_swap_interval(interval);
}

void *nack__gl_get_proc_address(const char *name)
{
    if (!nack__g.initialized || !name || !nack__g.vt->gl_get_proc_address)
        return NULL;
    return nack__proc_cache_get(name, nack__g.vt->gl_get_proc_address);
}

/* ------------------------------------------------------------------ */
/* Clipboard                                                          */
/* ------------------------------------------------------------------ */

bool nack__win_clipboard_set(const char *utf8)
{
    NACK_REQUIRE_INIT_RET(false);
    if (!utf8 || !nack__g.vt->clipboard_set)
        return false;
    return nack__g.vt->clipboard_set(utf8);
}

const char *nack__win_clipboard_get(void)
{
    if (!nack__g.initialized || !nack__g.vt->clipboard_get)
        return NULL;
    return nack__g.vt->clipboard_get();
}

bool nack__win_primary_set(const char *utf8)
{
    if (!nack__g.initialized || !utf8 || !nack__g.vt->primary_set)
        return false;
    return nack__g.vt->primary_set(utf8);
}

const char *nack__win_primary_get(void)
{
    if (!nack__g.initialized || !nack__g.vt->primary_get)
        return NULL;
    return nack__g.vt->primary_get();
}
