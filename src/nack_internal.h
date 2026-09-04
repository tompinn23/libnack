/* Internal shared definitions for libnack backends. */
#ifndef NACK_INTERNAL_H_INCLUDED
#define NACK_INTERNAL_H_INCLUDED

/*
 * The library targets C99, but needs POSIX for clock_gettime, poll and pipe.
 * Requesting them explicitly keeps a strict -std=c99 build working instead of
 * relying on the compiler's GNU dialect. This has to come before any system
 * header is pulled in.
 */
#if !defined(_WIN32)
#  if defined(__APPLE__)
#    ifndef _DARWIN_C_SOURCE
#      define _DARWIN_C_SOURCE 1
#    endif
#  else
#    ifndef _POSIX_C_SOURCE
#      define _POSIX_C_SOURCE 200809L
#    endif
#    ifndef _DEFAULT_SOURCE
#      define _DEFAULT_SOURCE 1
#    endif
#  endif
#endif

#include "nack_core.h"
#include "nack_window.h"

/*
 * C++ only, like the console's internal header and for the same reason: the
 * types below hold std::string, std::deque and std::vector. Nothing still-C
 * in tests/ reaches in here - see the comment at the top of nack_window.h.
 */
#ifndef __cplusplus
#  error "nack_internal.h is C++"
#endif

#include <array>
#include <climits>
#include <cstring>
#include <deque>
#include <exception>
#include <string>
#include <vector>


#if defined(_WIN32)
#  define NACK_PLATFORM_WIN32 1
#elif defined(__APPLE__)
#  define NACK_PLATFORM_COCOA 1
#else
#  define NACK_PLATFORM_UNIX 1
#endif

class nack_backend_vt;

struct nack_window {
    nack_backend_vt *vt = nullptr;
    void *native = nullptr;    /* backend-owned per-window state */

    std::string title;
    int width = 0, height = 0;           /* logical size               */
    int fb_width = 0, fb_height = 0;     /* framebuffer (physical) size */
    int pos_x = 0, pos_y = 0;
    float scale = 1.0f;

    int min_width = 0, min_height = 0;
    int max_width = 0, max_height = 0;
    int inc_width = 0, inc_height = 0;

    bool resizable = true;
    bool decorated = true;
    bool transparent = false;
    bool high_dpi = true;
    nack_framebuffer_desc framebuffer;
    bool visible = false;
    bool focused = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    bool should_close = false;
    bool destroyed = false;

    nack_cursor_mode cursor_mode = NACK_CURSOR_MODE_NORMAL;
    nack_cursor_shape cursor_shape = NACK_CURSOR_ARROW;

    double mouse_x = 0.0, mouse_y = 0.0;

    /* multi-click tracking */
    uint64_t last_click_time_ns = 0;
    double last_click_x = 0.0, last_click_y = 0.0;
    int last_click_button = -1;
    int click_count = 0;

    void *user_data = nullptr;

    /*
     * Creates a window. A visible window appears on every backend as soon as
     * it is shown, even before anything has been drawn into it: on Wayland,
     * where a surface with no committed buffer would never be mapped at all,
     * the library commits a blank frame and retires it on the first
     * gl_swap_buffers.
     */
    static nack_window *create(const nack_window_desc *desc);
    /* Tolerant of NULL, and safe to call more than once on the same window. */
    static void destroy(nack_window *window);

    void register_self();
    void unregister_self();

    void show();
    void hide();
    void focus();
    void minimize();
    void maximize();
    void restore();
    void request_attention();

    void set_title(const char *title);
    void set_size(int width, int height);
    void set_position(int x, int y);
    void set_size_limits(int min_width, int min_height, int max_width,
                         int max_height);
    void set_size_increments(int dw, int dh);
    void set_fullscreen(bool fullscreen);
    void set_should_close(bool value);
    void set_cursor_shape(nack_cursor_shape shape);
    void set_cursor_mode(nack_cursor_mode mode);
    /* Marks the whole window as needing a repaint; produces a WINDOW_EXPOSE. */
    void request_redraw();
    void get_native(nack_native_window *out) const;

    /* Convenience emitters used by backends */
    void emit_simple(nack_win_event_type type);
    void emit_resize(int width, int height, int fb_width, int fb_height);
    void emit_scale(float scale);
    void emit_focus(bool focused);
    void emit_key(nack_key key, uint32_t scancode, uint32_t mods,
                 bool down, bool repeat);
    void emit_text(const char *utf8);
    void emit_mouse_move(double x, double y, uint32_t mods);
    void emit_mouse_button(int button, bool down, double x, double y,
                           uint32_t mods);
    void emit_scroll(double dx, double dy, uint32_t mods, bool precise);
};

struct nack_gl_context {
    nack_backend_vt *vt = nullptr;
    void *native = nullptr;
    nack_window *owner = nullptr;   /* window whose pixel format the context matches */

    static nack_gl_context *create(nack_window *window,
                                          const nack__gl_desc *desc);
    static void destroy(nack_gl_context *context);
};

/*
 * One windowing system.
 *
 * A class rather than a table of function pointers, and for the same reason
 * the renderer is: the choice is made at run time - Wayland falling back to
 * X11, NACK_BACKEND picking between them - so a template cannot express it.
 *
 * The gain over the table is narrower here than it was for the renderer,
 * because these were already filled in by field name rather than positionally.
 * What it adds is that leaving an operation out is a compile error instead of
 * a NULL slot, and that the three operations which are genuinely optional say
 * so once, here, instead of every caller having to guard them. All three are
 * Wayland: a client there cannot focus itself, cannot place itself, and needs
 * xdg-activation to ask for attention.
 */
class nack_backend_vt {
public:
    virtual ~nack_backend_vt() = default;

    virtual const char *name() const = 0;
    virtual nack_backend id() const = 0;

    virtual bool init(const nack_win_init_desc *desc) = 0;
    virtual void shutdown() = 0;

    virtual bool window_create(nack_window *w,
                               const nack_window_desc *desc) = 0;
    virtual void window_destroy(nack_window *w) = 0;
    virtual void window_show(nack_window *w, bool show) = 0;
    virtual void window_set_title(nack_window *w, const char *title) = 0;
    virtual void window_set_size(nack_window *w, int width,
                                 int height) = 0;
    virtual void window_apply_size_hints(nack_window *w) = 0;
    virtual void window_set_fullscreen(nack_window *w,
                                       bool fullscreen) = 0;
    virtual void window_minimize(nack_window *w) = 0;
    virtual void window_maximize(nack_window *w) = 0;
    virtual void window_restore(nack_window *w) = 0;
    virtual void window_request_redraw(nack_window *w) = 0;
    virtual void window_set_cursor_shape(nack_window *w,
                                         nack_cursor_shape shape) = 0;
    virtual void window_set_cursor_mode(nack_window *w,
                                        nack_cursor_mode mode) = 0;
    virtual void window_get_native(const nack_window *w,
                                   nack_native_window *out) = 0;

    /*
     * Not every windowing system offers these. Declining is what the default
     * means; it is not the same as forgetting, which no longer compiles.
     */
    virtual void window_focus(nack_window *w) { (void)w; }
    virtual void window_set_position(nack_window *w, int x, int y)
    {
        (void)w; (void)x; (void)y;
    }
    virtual void window_request_attention(nack_window *w) { (void)w; }

    /* Pump platform events into the queue. timeout < 0 blocks indefinitely,
     * 0 polls, > 0 waits at most that many seconds. */
    virtual void pump_events(double timeout) = 0;
    virtual void wakeup() = 0;

    virtual nack_gl_context *gl_create(nack_window *w,
                                              const nack__gl_desc *desc) = 0;
    virtual void gl_destroy(nack_gl_context *ctx) = 0;
    virtual bool gl_make_current(nack_window *w,
                                 nack_gl_context *ctx) = 0;
    virtual void gl_swap_buffers(nack_window *w) = 0;
    virtual void gl_set_swap_interval(int interval) = 0;
    virtual void *gl_get_proc_address(const char *name) = 0;

    virtual bool clipboard_set(const char *utf8) = 0;
    virtual const char *clipboard_get() = 0;

    /*
     * The PRIMARY selection - select to copy, middle click to paste - is an
     * X11 idea. Windows and macOS have no equivalent, and say so by leaving
     * these alone rather than by a NULL slot the caller had to know about.
     */
    virtual bool primary_set(const char *utf8) { (void)utf8; return false; }
    virtual const char *primary_get() { return NULL; }
};

/* ------------------------------------------------------------------ */
/* Shared state, defined in nack.c                                    */
/* ------------------------------------------------------------------ */

struct nack_state {
    bool initialized = false;
    nack_backend_vt *vt = nullptr;
    std::string app_id;

    void (*log_fn)(const char *, void *) = nullptr;
    void *log_user_data = nullptr;

    /*
     * Events are only ever produced inside pump_events, which the same thread
     * that drains them calls, so the queue holds at most one pump's worth and
     * needs no cap. The ring buffer it replaces had one, and dropped the
     * oldest event to stay within it.
     */
    std::deque<nack_win_event> queue;

    std::vector<nack_window *> windows;

    std::array<bool, NACK_KEY_COUNT> keys{};
    std::array<bool, NACK_MOUSE_BUTTON_COUNT> mouse_buttons{};
    uint32_t mods = 0;

    nack_gl_context *current_context = nullptr;
    nack_window *current_window = nullptr;
    int swap_interval = 0;

    nack_result error_code = NACK_OK;
    std::string error_message;

    bool init(const nack_win_init_desc *desc);
    void shutdown();
    nack_backend backend() const;

    /* Last error for the calling thread; valid until the next failing call. */
    nack_result last_error(const char **message) const;
    bool fail(nack_result code, const char *fmt, ...);

    /* Event queue */
    void push_event(const nack_win_event *ev);
    nack_win_event *event_begin(nack_win_event_type type,
                                       nack_window *w);
    /* For events - QUIT, WAKEUP - with no window to be a method on. */
    void emit_global(nack_win_event_type type);

    /* Non-blocking: returns false when the queue is empty. */
    bool poll_event(nack_win_event *event);
    /* Blocks until at least one event is available. */
    bool wait_event(nack_win_event *event);
    /* Blocks for at most `timeout` seconds; returns false on timeout. */
    bool wait_event_timeout(nack_win_event *event, double timeout);
    /* Thread-safe: unblocks a waiting wait_event* and queues NACK_WIN_EVENT_WAKEUP. */
    void wakeup();

    /* Instantaneous input state, updated as events are generated. */
    bool key_is_down(nack_key key) const;
    uint32_t get_mods() const;
    bool mouse_button_is_down(int button) const;

    bool gl_make_current(nack_window *window,
                         nack_gl_context *context);
    void gl_swap_buffers(nack_window *window);
    void gl_set_swap_interval(int interval);
    /*
     * Resolves an OpenGL entry point. Suitable as the loader callback for
     * glad, epoxy, or a hand-rolled loader; results are cached internally, so
     * resolving a few hundred names at startup is cheap and repeat loads are
     * cheaper.
     *
     * A non-NULL result does NOT prove the function is usable: some drivers
     * (anything on libglvnd) return a dispatch stub for every gl-prefixed
     * name. Gate optional functionality on the context version or on
     * gl_extension_supported(), never on the pointer alone.
     */
    void *gl_get_proc_address(const char *name);
    bool gl_extension_supported(const char *name);

    bool clipboard_set(const char *utf8);
    /* Returned string is owned by the library and valid until the next call. */
    const char *clipboard_get();

    /* Primary selection (X11/Wayland). Returns false/NULL elsewhere. */
    bool primary_set(const char *utf8);
    const char *primary_get();
};

extern nack_state state;

/* Diagnostics */
void nack_log(const char *fmt, ...);

/*
 * The window layer's half of the C/C++ boundary. Same job as
 * console/nack_guard.h - turn a container's exception back into the false or
 * NULL a C caller expects - but it reports through state.fail(), which is
 * where this layer keeps its error.
 */
namespace nack {

template <class Body, class Result>
Result guarded_win(const char *what, Body &&body, Result on_error)
{
    try {
        return body();
    } catch (const std::exception &failure) {
        state.fail(NACK_ERROR_OUT_OF_MEMORY, "%s: %s", what, failure.what());
        return on_error;
    }
}

}   /* namespace nack */

/* GL entry-point lookup cache (src/common/nack_proc_cache.c) */
void *nack__proc_cache_get(const char *name, void *(*resolve)(const char *));
void  nack__proc_cache_clear(void);

/* out must have room for 4 bytes plus a terminator. Spelled [5] rather
 * than the C99 [static 5], which MSVC does not implement. */
uint32_t nack__utf8_encode(uint32_t codepoint, char out[5]);
bool nack__codepoint_is_text(uint32_t codepoint);

/* Backend registration - each platform provides the ones it supports. */
nack_backend_vt *nack__backend_win32(void);
nack_backend_vt *nack__backend_cocoa(void);
nack_backend_vt *nack__backend_wayland(void);
nack_backend_vt *nack__backend_x11(void);

#endif /* NACK_INTERNAL_H_INCLUDED */
