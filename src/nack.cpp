/* libnack - platform independent core: dispatch, event queue, window shell. */
#include "nack_internal.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <exception>
#include <memory>

#if defined(NACK_PLATFORM_WIN32)
#  if defined(NACK_WIN32_USE_SDK_HEADERS)
#    include <windows.h>
#  else
#    include "win32/nack_win32_api.h"
#  endif
#else
#  include <time.h>
#endif

nack_state state;

#define NACK_DOUBLE_CLICK_NS      400000000ull   /* 400 ms */
#define NACK_DOUBLE_CLICK_SLOP    4.0            /* logical pixels */

/* ------------------------------------------------------------------ */
/* Diagnostics                                                        */
/* ------------------------------------------------------------------ */

void nack_log(const char *fmt, ...)
{
    if (!state.log_fn)
        return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    state.log_fn(buf, state.log_user_data);
}

bool nack_state::fail(nack_result code, const char *fmt, ...)
{
    char message[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(message, sizeof message, fmt, ap);
    va_end(ap);

    /* Recording a failure must not be the thing that fails. */
    try {
        error_message = message;
    } catch (const std::exception &) {
    }
    error_code = code;
    nack_log("nack: error: %s", message);
    return false;
}

nack_result nack_state::last_error(const char **message) const
{
    if (message)
        *message = error_message.c_str();
    return error_code;
}

namespace nack { namespace detail {

uint32_t utf8_encode(uint32_t cp, char out[5])
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
bool codepoint_is_text(uint32_t cp)
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

uint64_t win_time_ns(void)
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

double win_time_seconds(void)
{
    return (double)win_time_ns() / 1e9;
}

} }   /* namespace nack::detail */

/* ------------------------------------------------------------------ */
/* Event queue                                                        */
/* ------------------------------------------------------------------ */

void nack_state::push_event(const nack_win_event *ev)
{
    /*
     * An event that cannot be queued is an event the application never sees,
     * so a failure here is dropped rather than reported: there is no caller
     * to report it to, and the alternative is throwing out of a backend's
     * event handler.
     */
    try {
        queue.push_back(*ev);
    } catch (const std::exception &) {
        nack_log("nack: dropped an event: out of memory");
    }
}

nack_win_event *nack_state::event_begin(nack_win_event_type type,
                                               nack_window *w)
{
    static nack_win_event scratch;
    memset(&scratch, 0, sizeof scratch);
    scratch.type = type;
    scratch.window = w;
    scratch.time_ns = win_time_ns();
    return &scratch;
}

void nack_state::emit_global(nack_win_event_type type)
{
    nack_win_event *ev = event_begin(type, nullptr);
    push_event(ev);
}

namespace nack { namespace detail {

static bool pop(nack_win_event *out)
{
    if (state.queue.empty())
        return false;
    *out = state.queue.front();
    state.queue.pop_front();
    return true;
}

} }   /* namespace nack::detail */

/* ------------------------------------------------------------------ */
/* Event emitters                                                     */
/* ------------------------------------------------------------------ */

void nack_window::emit_simple(nack_win_event_type type)
{
    nack_win_event *ev = state.event_begin(type, this);
    state.push_event(ev);
}

void nack_window::emit_resize(int new_width, int new_height, int new_fb_width,
                              int new_fb_height)
{
    if (new_width < 1) new_width = 1;
    if (new_height < 1) new_height = 1;
    if (new_fb_width < 1) new_fb_width = new_width;
    if (new_fb_height < 1) new_fb_height = new_height;

    if (width == new_width && height == new_height &&
        fb_width == new_fb_width && fb_height == new_fb_height)
        return;

    width = new_width;
    height = new_height;
    fb_width = new_fb_width;
    fb_height = new_fb_height;

    nack_win_event *ev = state.event_begin(NACK_WIN_EVENT_WINDOW_RESIZE, this);
    ev->data.size.width = new_width;
    ev->data.size.height = new_height;
    ev->data.size.fb_width = new_fb_width;
    ev->data.size.fb_height = new_fb_height;
    state.push_event(ev);
}

void nack_window::emit_scale(float new_scale)
{
    if (new_scale <= 0.0f || scale == new_scale)
        return;
    scale = new_scale;
    nack_win_event *ev = state.event_begin(NACK_WIN_EVENT_WINDOW_SCALE, this);
    ev->data.scale.scale = new_scale;
    state.push_event(ev);
}

void nack_window::emit_focus(bool is_focused)
{
    if (focused == is_focused)
        return;
    focused = is_focused;
    if (!is_focused) {
        /* Never leave keys latched when focus leaves the window. */
        for (int i = 0; i < NACK_KEY_COUNT; ++i) {
            if (state.keys[i]) {
                state.keys[i] = false;
                emit_key((nack_key)i, 0, state.mods, false, false);
            }
        }
        state.mods = 0;
    }
    emit_simple(is_focused ? NACK_WIN_EVENT_WINDOW_FOCUS : NACK_WIN_EVENT_WINDOW_BLUR);
}

void nack_window::emit_key(nack_key key, uint32_t scancode, uint32_t mods,
                          bool down, bool repeat)
{
    if (key > 0 && key < NACK_KEY_COUNT) {
        if (!down && !state.keys[key] && !repeat) {
            /* Release without a matching press (focus loss, grab). Still
             * report it: consumers track their own state. */
        }
        state.keys[key] = down;
    }
    state.mods = mods;

    nack_win_event *ev =
        state.event_begin(down ? NACK_WIN_EVENT_KEY_DOWN : NACK_WIN_EVENT_KEY_UP, this);
    ev->data.key.key = key;
    ev->data.key.scancode = scancode;
    ev->data.key.mods = mods;
    ev->data.key.repeat = repeat;
    state.push_event(ev);
}

void nack_window::emit_text(const char *utf8)
{
    if (!utf8 || !utf8[0])
        return;
    nack_win_event *ev = state.event_begin(NACK_WIN_EVENT_TEXT, this);
    size_t n = strlen(utf8);
    if (n >= sizeof ev->data.text.utf8)
        n = sizeof ev->data.text.utf8 - 1;
    memcpy(ev->data.text.utf8, utf8, n);
    ev->data.text.utf8[n] = '\0';
    state.push_event(ev);
}

void nack_window::emit_mouse_move(double x, double y, uint32_t mods)
{
    nack_win_event *ev = state.event_begin(NACK_WIN_EVENT_MOUSE_MOVE, this);
    ev->data.motion.dx = x - mouse_x;
    ev->data.motion.dy = y - mouse_y;
    ev->data.motion.x = x;
    ev->data.motion.y = y;
    ev->data.motion.mods = mods;
    mouse_x = x;
    mouse_y = y;
    state.mods = mods;
    state.push_event(ev);
}

void nack_window::emit_mouse_button(int button, bool down, double x, double y,
                                    uint32_t mods)
{
    if (button < 0 || button >= NACK_MOUSE_BUTTON_COUNT)
        return;
    state.mouse_buttons[button] = down;
    state.mods = mods;
    mouse_x = x;
    mouse_y = y;

    int clicks = 1;
    if (down) {
        uint64_t now = win_time_ns();
        double ddx = x - last_click_x;
        double ddy = y - last_click_y;
        /* Not named `near`: windows.h still defines that as a macro. */
        bool within_slop = (ddx * ddx + ddy * ddy) <=
                           (NACK_DOUBLE_CLICK_SLOP * NACK_DOUBLE_CLICK_SLOP);
        if (button == last_click_button && within_slop &&
            now - last_click_time_ns <= NACK_DOUBLE_CLICK_NS) {
            click_count++;
        } else {
            click_count = 1;
        }
        last_click_time_ns = now;
        last_click_x = x;
        last_click_y = y;
        last_click_button = button;
        clicks = click_count;
    } else {
        clicks = click_count ? click_count : 1;
    }

    nack_win_event *ev =
        state.event_begin(down ? NACK_WIN_EVENT_MOUSE_DOWN : NACK_WIN_EVENT_MOUSE_UP, this);
    ev->data.button.button = button;
    ev->data.button.x = x;
    ev->data.button.y = y;
    ev->data.button.mods = mods;
    ev->data.button.click_count = clicks;
    state.push_event(ev);
}

void nack_window::emit_scroll(double dx, double dy, uint32_t mods, bool precise)
{
    if (dx == 0.0 && dy == 0.0)
        return;
    nack_win_event *ev = state.event_begin(NACK_WIN_EVENT_MOUSE_SCROLL, this);
    ev->data.scroll.dx = dx;
    ev->data.scroll.dy = dy;
    ev->data.scroll.mods = mods;
    ev->data.scroll.precise = precise;
    state.push_event(ev);
}

/* ------------------------------------------------------------------ */
/* Window registry                                                    */
/* ------------------------------------------------------------------ */

void nack_window::register_self()
{
    state.windows.push_back(this);
}

void nack_window::unregister_self()
{
    auto at = std::find(state.windows.begin(), state.windows.end(), this);
    if (at != state.windows.end())
        state.windows.erase(at);
}

namespace nack { namespace detail {

/* Drop queued events referring to a window that is going away. */
static void purge_window_events(nack_window *w)
{
    auto stale = [w](const nack_win_event &ev) { return ev.window == w; };
    state.queue.erase(std::remove_if(state.queue.begin(),
                                       state.queue.end(), stale),
                        state.queue.end());
}

} }   /* namespace nack::detail */

/* ------------------------------------------------------------------ */
/* Backend selection                                                  */
/* ------------------------------------------------------------------ */

namespace nack { namespace detail {

const char *win_backend_name(nack_backend backend)
{
    switch (backend) {
    case NACK_BACKEND_WIN32:   return "win32";
    case NACK_BACKEND_COCOA:   return "cocoa";
    case NACK_BACKEND_WAYLAND: return "wayland";
    case NACK_BACKEND_X11:     return "x11";
    default:                   return "none";
    }
}

static nack_backend_vt *select_backend(nack_backend preferred)
{
#if defined(NACK_PLATFORM_WIN32)
    (void)preferred;
    return backend_win32();
#elif defined(NACK_PLATFORM_COCOA)
    (void)preferred;
    return backend_cocoa();
#elif defined(NACK_HAS_WAYLAND) && defined(NACK_HAS_X11)
    const char *env = getenv("NACK_BACKEND");
    if (env && *env) {
        if (strcmp(env, "wayland") == 0)
            preferred = NACK_BACKEND_WAYLAND;
        else if (strcmp(env, "x11") == 0 || strcmp(env, "xcb") == 0)
            preferred = NACK_BACKEND_X11;
    }

    if (preferred == NACK_BACKEND_X11)
        return backend_x11();
    if (preferred == NACK_BACKEND_WAYLAND)
        return backend_wayland();

    /* Auto: a compositor socket in the environment means a Wayland session,
     * even when an Xwayland DISPLAY is also present. */
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display && *wayland_display)
        return backend_wayland();
    return backend_x11();
#elif defined(NACK_HAS_WAYLAND)
    (void)preferred;
    return backend_wayland();
#elif defined(NACK_HAS_X11)
    (void)preferred;
    return backend_x11();
#else
    (void)preferred;
    return NULL;
#endif
}

/* Try each available Unix backend in order until one initializes. */
static bool try_init_backends(const nack_win_init_desc *desc)
{
#if defined(NACK_PLATFORM_UNIX)
    nack_backend_vt *order[2] = { nullptr, nullptr };
    size_t n = 0;
    nack_backend_vt *first = select_backend(desc->backend);
    if (!first)
        return state.fail(NACK_ERROR_NO_BACKEND, "no display backend compiled in");
    order[n++] = first;
#if defined(NACK_HAS_WAYLAND) && defined(NACK_HAS_X11)
    order[n++] = (first->id() == NACK_BACKEND_WAYLAND) ? backend_x11()
                                                     : backend_wayland();
#endif
    for (size_t i = 0; i < n; ++i) {
        if (!order[i])
            continue;
        state.vt = order[i];
        if (order[i]->init(desc))
            return true;
        nack_log("nack: backend '%s' unavailable: %s", order[i]->name(),
                  state.error_message.c_str());
        state.vt = nullptr;
    }
    return state.fail(NACK_ERROR_NO_BACKEND,
                      "no usable display backend (tried wayland/x11)");
#else
    nack_backend_vt *vt = select_backend(desc->backend);
    if (!vt)
        return state.fail(NACK_ERROR_NO_BACKEND, "no display backend compiled in");
    state.vt = vt;
    if (vt->init(desc))
        return true;
    state.vt = nullptr;
    return false;
#endif
}

} }   /* namespace nack::detail */

bool nack_state::init(const nack_win_init_desc *desc)
{
    if (initialized)
        return true;

    nack_win_init_desc local;
    if (desc)
        local = *desc;

    *this = nack_state{};
    log_fn = local.log_fn;
    log_user_data = local.log_user_data;
    swap_interval = 1;

    return nack::guarded_win("cannot start the window layer", [&] {
        app_id = local.app_id ? local.app_id : "libnack";
        if (!try_init_backends(&local)) {
            app_id.clear();
            return false;
        }
        initialized = true;
        nack_log("nack: using %s backend", vt->name());
        return true;
    }, false);
}

void nack_state::shutdown()
{
    if (!initialized)
        return;

    while (!windows.empty())
        nack_window::destroy(windows.back());

    if (vt)
        vt->shutdown();

    /* Entry points belong to the driver we are dropping. */
    proc_cache_clear();

    /* Assignment, not memset: the state owns strings and a queue now. */
    *this = nack_state{};
}

nack_backend nack_state::backend() const
{
    return vt ? vt->id() : NACK_BACKEND_NONE;
}

/* ------------------------------------------------------------------ */
/* Windows                                                            */
/* ------------------------------------------------------------------ */

#define NACK_REQUIRE_INIT_RET(ret)                                            \
    do {                                                                      \
        if (!state.initialized) {                                           \
            state.fail(NACK_ERROR_NOT_INITIALIZED, "nack_state::init() not called"); \
            return ret;                                                       \
        }                                                                     \
    } while (0)

#define NACK_REQUIRE_INIT() NACK_REQUIRE_INIT_RET()

nack_window *nack_window::create(const nack_window_desc *desc)
{
    NACK_REQUIRE_INIT_RET(nullptr);

    nack_window_desc d;
    if (desc) {
        d = *desc;
        if (!d.title) d.title = "libnack";
        if (d.width <= 0) d.width = 800;
        if (d.height <= 0) d.height = 600;
        /* A caller that memset its descriptor gets a usable format rather
         * than a request for a 0-bit framebuffer. */
        if (d.framebuffer.red_bits == 0 && d.framebuffer.green_bits == 0 &&
            d.framebuffer.blue_bits == 0 && d.framebuffer.depth_bits == 0)
            d.framebuffer = nack_framebuffer_desc{};
    }
    if (d.transparent && d.framebuffer.alpha_bits == 0)
        d.framebuffer.alpha_bits = 8;

    auto owner = nack::guarded_win("cannot create a window", [&] {
        return std::make_unique<nack_window>();
    }, std::unique_ptr<nack_window>());
    if (!owner)
        return nullptr;

    nack_window *w = owner.get();
    w->vt = state.vt;
    w->title = d.title ? d.title : "";
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

    if (!state.vt->window_create(w, &d))
        return nullptr;

    if (!nack::guarded_win("cannot register the window",
                           [&] { w->register_self(); return true; },
                           false)) {
        w->vt->window_destroy(w);
        return nullptr;
    }
    owner.release();

    if (d.fullscreen)
        w->set_fullscreen(true);
    else if (d.maximized)
        w->maximize();

    if (d.visible)
        w->show();

    return w;
}

void nack_window::destroy(nack_window *window)
{
    if (!window || window->destroyed)
        return;
    window->destroyed = true;

    if (state.current_window == window) {
        state.current_window = nullptr;
        state.current_context = nullptr;
    }

    purge_window_events(window);
    window->unregister_self();
    window->vt->window_destroy(window);
    delete window;
}

void nack_window::show()
{
    vt->window_show(this, true);
    visible = true;
}

void nack_window::hide()
{
    vt->window_show(this, false);
    visible = false;
}

void nack_window::focus()
{
    vt->window_focus(this);
}

void nack_window::minimize()
{
    vt->window_minimize(this);
}

void nack_window::maximize()
{
    vt->window_maximize(this);
}

void nack_window::restore()
{
    vt->window_restore(this);
}

void nack_window::request_attention()
{
    vt->window_request_attention(this);
}

void nack_window::set_title(const char *new_title)
{
    if (!new_title)
        return;
    if (!nack::guarded_win("cannot set the window title",
                           [&] { title = new_title; return true; }, false))
        return;
    vt->window_set_title(this, new_title);
}

void nack_window::set_size(int new_width, int new_height)
{
    if (new_width < 1 || new_height < 1)
        return;
    vt->window_set_size(this, new_width, new_height);
}

void nack_window::set_position(int x, int y)
{
    vt->window_set_position(this, x, y);
}

void nack_window::set_size_limits(int new_min_width, int new_min_height,
                                  int new_max_width, int new_max_height)
{
    min_width = new_min_width > 0 ? new_min_width : 0;
    min_height = new_min_height > 0 ? new_min_height : 0;
    max_width = new_max_width > 0 ? new_max_width : 0;
    max_height = new_max_height > 0 ? new_max_height : 0;
    vt->window_apply_size_hints(this);
}

void nack_window::set_size_increments(int dw, int dh)
{
    inc_width = dw > 0 ? dw : 0;
    inc_height = dh > 0 ? dh : 0;
    vt->window_apply_size_hints(this);
}

void nack_window::set_fullscreen(bool new_fullscreen)
{
    vt->window_set_fullscreen(this, new_fullscreen);
}

void nack_window::set_should_close(bool value)
{
    should_close = value;
}

void nack_window::set_cursor_shape(nack_cursor_shape shape)
{
    if (shape < 0 || shape >= NACK_CURSOR_SHAPE_COUNT)
        return;
    cursor_shape = shape;
    vt->window_set_cursor_shape(this, shape);
}

void nack_window::set_cursor_mode(nack_cursor_mode mode)
{
    cursor_mode = mode;
    vt->window_set_cursor_mode(this, mode);
}

void nack_window::request_redraw()
{
    vt->window_request_redraw(this);
}

void nack_window::get_native(nack_native_window *out) const
{
    if (!out)
        return;
    memset(out, 0, sizeof *out);
    out->backend = vt->id();
    vt->window_get_native(this, out);
}

/* ------------------------------------------------------------------ */
/* Event loop                                                         */
/* ------------------------------------------------------------------ */

bool nack_state::poll_event(nack_win_event *event)
{
    NACK_REQUIRE_INIT_RET(false);
    if (!event)
        return false;
    if (pop(event))
        return true;
    vt->pump_events(0.0);
    return pop(event);
}

bool nack_state::wait_event(nack_win_event *event)
{
    return wait_event_timeout(event, -1.0);
}

bool nack_state::wait_event_timeout(nack_win_event *event, double timeout)
{
    NACK_REQUIRE_INIT_RET(false);
    if (!event)
        return false;
    if (pop(event))
        return true;

    if (timeout < 0.0) {
        /* Block until the platform produces something we can hand back. */
        while (queue.empty()) {
            vt->pump_events(-1.0);
            if (windows.empty() && queue.empty())
                return false;   /* nothing left that could wake us */
        }
        return pop(event);
    }

    double deadline = win_time_seconds() + timeout;
    for (;;) {
        double remaining = deadline - win_time_seconds();
        if (remaining < 0.0)
            remaining = 0.0;
        vt->pump_events(remaining);
        if (pop(event))
            return true;
        if (win_time_seconds() >= deadline)
            return false;
    }
}

void nack_state::wakeup()
{
    if (initialized)
        vt->wakeup();
}

bool nack_state::key_is_down(nack_key key) const
{
    if (key <= 0 || key >= NACK_KEY_COUNT)
        return false;
    return keys[key];
}

uint32_t nack_state::get_mods() const
{
    return mods;
}

bool nack_state::mouse_button_is_down(int button) const
{
    if (button < 0 || button >= NACK_MOUSE_BUTTON_COUNT)
        return false;
    return mouse_buttons[button];
}

/* ------------------------------------------------------------------ */
/* OpenGL                                                             */
/* ------------------------------------------------------------------ */

nack_gl_context *nack_gl_context::create(nack_window *window,
                                                const gl_desc *desc)
{
    NACK_REQUIRE_INIT_RET(nullptr);
    if (!window) {
        state.fail(NACK_ERROR_INVALID_ARGUMENT, "nack_gl_context::create: null window");
        return nullptr;
    }
    gl_desc d;
    if (desc)
        d = *desc;
    return state.vt->gl_create(window, &d);
}

void nack_gl_context::destroy(nack_gl_context *context)
{
    if (!context)
        return;
    if (state.current_context == context) {
        state.current_context = nullptr;
        state.current_window = nullptr;
    }
    context->vt->gl_destroy(context);
}

bool nack_state::gl_make_current(nack_window *window, nack_gl_context *context)
{
    NACK_REQUIRE_INIT_RET(false);
    if (!vt->gl_make_current(window, context))
        return false;
    current_window = context ? window : nullptr;
    current_context = context;
    return true;
}

void nack_state::gl_swap_buffers(nack_window *window)
{
    if (window && vt)
        vt->gl_swap_buffers(window);
}

void nack_state::gl_set_swap_interval(int interval)
{
    if (!initialized)
        return;
    swap_interval = interval;
    vt->gl_set_swap_interval(interval);
}

void *nack_state::gl_get_proc_address(const char *name)
{
    if (!initialized || !name)
        return nullptr;
    /* The cache takes a plain function; a capture-less lambda is one. */
    return proc_cache_get(name, [](const char *symbol) {
        return state.vt->gl_get_proc_address(symbol);
    });
}

/* ------------------------------------------------------------------ */
/* Clipboard                                                          */
/* ------------------------------------------------------------------ */

bool nack_state::clipboard_set(const char *utf8)
{
    NACK_REQUIRE_INIT_RET(false);
    if (!utf8)
        return fail(NACK_ERROR_INVALID_ARGUMENT, "no text to set");
    return vt->clipboard_set(utf8);
}

const char *nack_state::clipboard_get()
{
    if (!initialized)
        return nullptr;
    return vt->clipboard_get();
}

bool nack_state::primary_set(const char *utf8)
{
    if (!initialized || !utf8)
        return false;
    return vt->primary_set(utf8);
}

const char *nack_state::primary_get()
{
    if (!initialized)
        return nullptr;
    return vt->primary_get();
}
