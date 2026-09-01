/*
 * Internal windowing and OpenGL context layer.
 *
 * This is no longer part of libnack's public API: the library exposes only the
 * cell console (include/nack/nack.h), and owns the window and GL context
 * behind it. Everything here is for the console renderer's use.
 *
 * Backends: Win32, Cocoa, Wayland, X11.
 *
 * The API is deliberately shaped for the needs of a grid/terminal renderer:
 * blocking event waits with timeouts, thread-safe wakeups, size increments,
 * per-monitor DPI scaling, clipboard access and UTF-8 text input.
 *
 * Threading: unless documented otherwise, every function must be called from
 * the thread that called nack__win_init(). The exceptions are nack__win_wakeup() and
 * nack__win_time_ns()/nack__win_time_seconds(), which are safe from any thread.
 */
#ifndef NACK_WINDOW_H_INCLUDED
#define NACK_WINDOW_H_INCLUDED

#include "nack/nack.h"   /* keys, mods, mouse buttons, colours */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Enumerations                                                               */
/* -------------------------------------------------------------------------- */

enum nack_backend {
    NACK_BACKEND_NONE = 0,
    NACK_BACKEND_WIN32,
    NACK_BACKEND_COCOA,
    NACK_BACKEND_WAYLAND,
    NACK_BACKEND_X11
};

enum nack_result {
    NACK_OK = 0,
    NACK_ERROR_UNKNOWN = -1,
    NACK_ERROR_NOT_INITIALIZED = -2,
    NACK_ERROR_NO_BACKEND = -3,
    NACK_ERROR_PLATFORM = -4,
    NACK_ERROR_OUT_OF_MEMORY = -5,
    NACK_ERROR_INVALID_ARGUMENT = -6,
    NACK_ERROR_NO_PIXEL_FORMAT = -7,
    NACK_ERROR_CONTEXT_CREATION = -8,
    NACK_ERROR_UNSUPPORTED = -9
};

enum nack_win_event_type {
    NACK_WIN_EVENT_NONE = 0,

    NACK_WIN_EVENT_WINDOW_CLOSE,       /* user asked to close the window        */
    NACK_WIN_EVENT_WINDOW_RESIZE,      /* ev.size                                */
    NACK_WIN_EVENT_WINDOW_MOVE,        /* ev.move                                */
    NACK_WIN_EVENT_WINDOW_FOCUS,
    NACK_WIN_EVENT_WINDOW_BLUR,
    NACK_WIN_EVENT_WINDOW_EXPOSE,      /* contents need repainting               */
    NACK_WIN_EVENT_WINDOW_SCALE,       /* ev.scale, DPI/content scale changed    */
    NACK_WIN_EVENT_WINDOW_MINIMIZE,
    NACK_WIN_EVENT_WINDOW_RESTORE,
    NACK_WIN_EVENT_WINDOW_MAXIMIZE,

    NACK_WIN_EVENT_KEY_DOWN,           /* ev.key                                 */
    NACK_WIN_EVENT_KEY_UP,             /* ev.key                                 */
    NACK_WIN_EVENT_TEXT,               /* ev.text, UTF-8 committed text          */

    NACK_WIN_EVENT_MOUSE_DOWN,         /* ev.button                              */
    NACK_WIN_EVENT_MOUSE_UP,           /* ev.button                              */
    NACK_WIN_EVENT_MOUSE_MOVE,         /* ev.motion                              */
    NACK_WIN_EVENT_MOUSE_SCROLL,       /* ev.scroll                              */
    NACK_WIN_EVENT_MOUSE_ENTER,
    NACK_WIN_EVENT_MOUSE_LEAVE,

    NACK_WIN_EVENT_WAKEUP,             /* produced by nack__win_wakeup()              */
    NACK_WIN_EVENT_QUIT                /* app-level quit request (macOS/Windows) */
};



/*
 * Physical key identifiers. Values are USB HID keyboard usage codes, so they
 * describe the *position* of the key rather than the symbol printed on it;
 * the symbol arrives separately as NACK_WIN_EVENT_TEXT.
 */

enum nack_cursor_shape {
    NACK_CURSOR_ARROW = 0,
    NACK_CURSOR_IBEAM,
    NACK_CURSOR_CROSSHAIR,
    NACK_CURSOR_HAND,
    NACK_CURSOR_RESIZE_H,
    NACK_CURSOR_RESIZE_V,
    NACK_CURSOR_RESIZE_NWSE,
    NACK_CURSOR_RESIZE_NESW,
    NACK_CURSOR_RESIZE_ALL,
    NACK_CURSOR_NOT_ALLOWED,
    NACK_CURSOR_WAIT,
    NACK_CURSOR_SHAPE_COUNT
};

enum nack_cursor_mode {
    NACK_CURSOR_MODE_NORMAL = 0,
    NACK_CURSOR_MODE_HIDDEN,     /* hidden while over the window            */
    NACK_CURSOR_MODE_CAPTURED    /* hidden + confined, relative motion only */
};

enum nack__gl_profile {
    NACK__GL_PROFILE_CORE = 0,
    NACK__GL_PROFILE_COMPAT,
    NACK__GL_PROFILE_ES
};

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

struct nack_window;
struct nack_gl_context;

/*
 * Window-level input, in pixels. The console layer turns this into the public
 * cell-based struct nack_event.
 */
struct nack_win_event {
    enum nack_win_event_type type;
    struct nack_window *window;
    uint64_t time_ns;
    union {
        struct { int width, height, fb_width, fb_height; } size;
        struct { int x, y; } move;
        struct { float scale; } scale;
        struct {
            enum nack_key key;
            uint32_t scancode;
            uint32_t mods;
            bool repeat;
        } key;
        struct { char utf8[32]; } text;
        struct {
            double x, y;
            double dx, dy;
            uint32_t mods;
        } motion;
        struct {
            int button;
            double x, y;
            uint32_t mods;
            int click_count;
        } button;
        struct {
            double dx, dy;
            uint32_t mods;
            bool precise;
        } scroll;
    } data;
};


struct nack_win_init_desc {
    /* Preferred backend, or NACK_BACKEND_NONE to auto-detect. On Unix the
     * NACK_BACKEND environment variable ("wayland"/"x11") overrides this. */
    enum nack_backend backend;
    /* Application identifier: Wayland xdg app-id and X11 WM_CLASS. */
    const char *app_id;
    /* Optional diagnostic sink; receives NUL-terminated messages. */
    void (*log_fn)(const char *message, void *user_data);
    void *log_user_data;
};

/*
 * Framebuffer format. This is a property of the window, not of the context:
 * on EGL and Wayland the window's visual is derived from the pixel format, so
 * it has to be fixed when the window is created.
 */
struct nack_framebuffer_desc {
    int red_bits, green_bits, blue_bits, alpha_bits;
    int depth_bits, stencil_bits;
    int samples;                  /* MSAA sample count, 0 = off              */
    bool srgb;
    bool double_buffer;
};

struct nack_window_desc {
    const char *title;
    int width, height;            /* logical pixels; <= 0 picks a default   */
    int min_width, min_height;    /* 0 = unconstrained                       */
    int max_width, max_height;    /* 0 = unconstrained                       */
    int width_increment;          /* 0 = none (struct cell-size snapping)           */
    int height_increment;
    bool resizable;
    bool decorated;
    bool visible;
    bool fullscreen;
    bool maximized;
    bool transparent;
    bool high_dpi;                /* opt in to a framebuffer at native DPI   */
    struct nack_framebuffer_desc framebuffer;
    void *user_data;
};

/* Context attributes. The framebuffer format comes from the window. */
struct nack__gl_desc {
    int major, minor;             /* 0 = "any", library picks a sane default */
    enum nack__gl_profile profile;
    bool debug;
    bool forward_compatible;
    bool robust;
    struct nack_gl_context *share;
};

/* -------------------------------------------------------------------------- */
/* Library lifetime                                                           */
/* -------------------------------------------------------------------------- */

bool         nack__win_init(const struct nack_win_init_desc *desc);
void         nack__win_shutdown(void);
bool         nack__win_is_initialized(void);
enum nack_backend nack__win_get_backend(void);
const char  *nack__win_backend_name(enum nack_backend backend);

/* Last error for the calling thread; valid until the next failing call. */
enum nack_result  nack__win_get_error(const char **message);

void nack_window_desc_defaults(struct nack_window_desc *desc);
void nack__gl_desc_defaults(struct nack__gl_desc *desc);
void nack_framebuffer_desc_defaults(struct nack_framebuffer_desc *desc);

/* -------------------------------------------------------------------------- */
/* Windows                                                                    */
/* -------------------------------------------------------------------------- */

/*
 * Creates a window. A visible window appears on every backend as soon as it is
 * shown, even before anything has been drawn into it: on Wayland, where a
 * surface with no committed buffer would never be mapped at all, the library
 * commits a blank frame and retires it on the first nack__gl_swap_buffers.
 */
struct nack_window *nack_window_create(const struct nack_window_desc *desc);
void         nack_window_destroy(struct nack_window *window);

void  nack_window_show(struct nack_window *window);
void  nack_window_hide(struct nack_window *window);
void  nack_window_focus(struct nack_window *window);
void  nack_window_minimize(struct nack_window *window);
void  nack_window_maximize(struct nack_window *window);
void  nack_window_restore(struct nack_window *window);
void  nack_window_request_attention(struct nack_window *window);

void  nack_window_set_title(struct nack_window *window, const char *title);
void  nack_window_set_size(struct nack_window *window, int width, int height);
void  nack_window_get_size(const struct nack_window *window, int *width,
                                    int *height);
void  nack_window_get_framebuffer_size(const struct nack_window *window,
                                                int *width, int *height);
void  nack_window_get_position(const struct nack_window *window, int *x, int *y);
void  nack_window_set_position(struct nack_window *window, int x, int y);
float nack_window_get_content_scale(const struct nack_window *window);

void  nack_window_set_size_limits(struct nack_window *window,
                                           int min_width, int min_height,
                                           int max_width, int max_height);
void  nack_window_set_size_increments(struct nack_window *window, int dw,
                                               int dh);

void  nack_window_set_fullscreen(struct nack_window *window, bool fullscreen);
bool  nack_window_is_fullscreen(const struct nack_window *window);
bool  nack_window_is_focused(const struct nack_window *window);
bool  nack_window_is_minimized(const struct nack_window *window);
bool  nack_window_is_maximized(const struct nack_window *window);

bool  nack_window_should_close(const struct nack_window *window);
void  nack_window_set_should_close(struct nack_window *window, bool value);

void *nack_window_get_user_data(const struct nack_window *window);
void  nack_window_set_user_data(struct nack_window *window, void *user_data);

void  nack_window_set_cursor_shape(struct nack_window *window,
                                            enum nack_cursor_shape shape);
void  nack_window_set_cursor_mode(struct nack_window *window,
                                           enum nack_cursor_mode mode);
enum nack_cursor_mode nack_window_get_cursor_mode(const struct nack_window *window);

/* Marks the whole window as needing a repaint; produces a WINDOW_EXPOSE. */
void  nack_window_request_redraw(struct nack_window *window);

/* -------------------------------------------------------------------------- */
/* Events                                                                     */
/* -------------------------------------------------------------------------- */

/* Non-blocking: returns false when the queue is empty. */
bool nack__win_poll_event(struct nack_win_event *event);
/* Blocks until at least one event is available. */
bool nack__win_wait_event(struct nack_win_event *event);
/* Blocks for at most `timeout` seconds; returns false on timeout. */
bool nack__win_wait_event_timeout(struct nack_win_event *event, double timeout);
/* Thread-safe: unblocks a waiting nack__win_wait_event* and queues NACK_WIN_EVENT_WAKEUP. */
void nack__win_wakeup(void);

/* Instantaneous input state, updated as events are generated. */
bool     nack__win_key_is_down(enum nack_key key);
uint32_t nack__win_get_mods(void);
bool     nack__win_mouse_button_is_down(int button);
void     nack__win_get_mouse_position(struct nack_window *window, double *x,
                                          double *y);

const char *nack_key_get_name(enum nack_key key);

/* -------------------------------------------------------------------------- */
/* OpenGL                                                                     */
/* -------------------------------------------------------------------------- */

struct nack_gl_context *nack__gl_context_create(struct nack_window *window, const struct nack__gl_desc *desc);
void  nack__gl_context_destroy(struct nack_gl_context *context);
bool  nack__gl_make_current(struct nack_window *window,
                                    struct nack_gl_context *context);
void  nack__gl_swap_buffers(struct nack_window *window);
void  nack__gl_set_swap_interval(int interval);
/*
 * Resolves an OpenGL entry point. Suitable as the loader callback for glad,
 * epoxy, or a hand-rolled loader; results are cached internally, so resolving
 * a few hundred names at startup is cheap and repeat loads are cheaper.
 *
 * A non-NULL result does NOT prove the function is usable: some drivers
 * (anything on libglvnd) return a dispatch stub for every gl-prefixed name.
 * Gate optional functionality on the context version or on
 * nack__gl_extension_supported(), never on the pointer alone.
 */
void *nack__gl_get_proc_address(const char *name);
bool  nack__gl_extension_supported(const char *name);

/* -------------------------------------------------------------------------- */
/* Clipboard                                                                  */
/* -------------------------------------------------------------------------- */

bool        nack__win_clipboard_set(const char *utf8);
/* Returned string is owned by the library and valid until the next call. */
const char *nack__win_clipboard_get(void);

/* Primary selection (X11/Wayland). Returns false/NULL elsewhere. */
bool        nack__win_primary_set(const char *utf8);
const char *nack__win_primary_get(void);

/* -------------------------------------------------------------------------- */
/* Time                                                                       */
/* -------------------------------------------------------------------------- */

uint64_t nack__win_time_ns(void);
double   nack__win_time_seconds(void);

/* -------------------------------------------------------------------------- */
/* Native handles (for interop; fields are backend-specific)                  */
/* -------------------------------------------------------------------------- */

struct nack_native_window {
    enum nack_backend backend;
    void *display;      /* Display* / wl_display* / NULL                     */
    void *surface;      /* wl_surface* / NSWindow* / HWND                    */
    void *view;         /* NSView*, which is what a CAMetalLayer attaches to */
    uintptr_t handle;   /* X11 Window id / HWND / 0                          */
};

void nack_window_get_native(const struct nack_window *window,
                                     struct nack_native_window *out);

#endif /* NACK_WINDOW_H_INCLUDED */
