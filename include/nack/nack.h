/*
 * libnack - a small cross-platform windowing + OpenGL context library.
 *
 * Backends: Win32, Cocoa, Wayland, X11.
 *
 * The API is deliberately shaped for the needs of a grid/terminal renderer:
 * blocking event waits with timeouts, thread-safe wakeups, size increments,
 * per-monitor DPI scaling, clipboard access and UTF-8 text input.
 *
 * Threading: unless documented otherwise, every function must be called from
 * the thread that called nack_init(). The exceptions are nack_wakeup() and
 * nack_time_ns()/nack_time_seconds(), which are safe from any thread.
 */
#ifndef NACK_H_INCLUDED
#define NACK_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(NACK_SHARED) && defined(_WIN32)
#  if defined(NACK_BUILD)
#    define NACK_API __declspec(dllexport)
#  else
#    define NACK_API __declspec(dllimport)
#  endif
#elif defined(NACK_SHARED) && defined(__GNUC__)
#  define NACK_API __attribute__((visibility("default")))
#else
#  define NACK_API extern
#endif

#define NACK_VERSION_MAJOR 0
#define NACK_VERSION_MINOR 1
#define NACK_VERSION_PATCH 0

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

enum nack_event_type {
    NACK_EVENT_NONE = 0,

    NACK_EVENT_WINDOW_CLOSE,       /* user asked to close the window        */
    NACK_EVENT_WINDOW_RESIZE,      /* ev.size                                */
    NACK_EVENT_WINDOW_MOVE,        /* ev.move                                */
    NACK_EVENT_WINDOW_FOCUS,
    NACK_EVENT_WINDOW_BLUR,
    NACK_EVENT_WINDOW_EXPOSE,      /* contents need repainting               */
    NACK_EVENT_WINDOW_SCALE,       /* ev.scale, DPI/content scale changed    */
    NACK_EVENT_WINDOW_MINIMIZE,
    NACK_EVENT_WINDOW_RESTORE,
    NACK_EVENT_WINDOW_MAXIMIZE,

    NACK_EVENT_KEY_DOWN,           /* ev.key                                 */
    NACK_EVENT_KEY_UP,             /* ev.key                                 */
    NACK_EVENT_TEXT,               /* ev.text, UTF-8 committed text          */

    NACK_EVENT_MOUSE_DOWN,         /* ev.button                              */
    NACK_EVENT_MOUSE_UP,           /* ev.button                              */
    NACK_EVENT_MOUSE_MOVE,         /* ev.motion                              */
    NACK_EVENT_MOUSE_SCROLL,       /* ev.scroll                              */
    NACK_EVENT_MOUSE_ENTER,
    NACK_EVENT_MOUSE_LEAVE,

    NACK_EVENT_WAKEUP,             /* produced by nack_wakeup()              */
    NACK_EVENT_QUIT                /* app-level quit request (macOS/Windows) */
};

enum nack_mod {
    NACK_MOD_SHIFT    = 1u << 0,
    NACK_MOD_CTRL     = 1u << 1,
    NACK_MOD_ALT      = 1u << 2,
    NACK_MOD_SUPER    = 1u << 3,   /* Windows key / Command                  */
    NACK_MOD_CAPSLOCK = 1u << 4,
    NACK_MOD_NUMLOCK  = 1u << 5
};

enum nack_mouse_button {
    NACK_MOUSE_LEFT = 0,
    NACK_MOUSE_RIGHT = 1,
    NACK_MOUSE_MIDDLE = 2,
    NACK_MOUSE_X1 = 3,
    NACK_MOUSE_X2 = 4,
    NACK_MOUSE_BUTTON_COUNT = 8
};

/*
 * Physical key identifiers. Values are USB HID keyboard usage codes, so they
 * describe the *position* of the key rather than the symbol printed on it;
 * the symbol arrives separately as NACK_EVENT_TEXT.
 */
enum nack_key {
    NACK_KEY_UNKNOWN = 0,

    NACK_KEY_A = 4, NACK_KEY_B, NACK_KEY_C, NACK_KEY_D, NACK_KEY_E, NACK_KEY_F,
    NACK_KEY_G, NACK_KEY_H, NACK_KEY_I, NACK_KEY_J, NACK_KEY_K, NACK_KEY_L,
    NACK_KEY_M, NACK_KEY_N, NACK_KEY_O, NACK_KEY_P, NACK_KEY_Q, NACK_KEY_R,
    NACK_KEY_S, NACK_KEY_T, NACK_KEY_U, NACK_KEY_V, NACK_KEY_W, NACK_KEY_X,
    NACK_KEY_Y, NACK_KEY_Z,

    NACK_KEY_1 = 30, NACK_KEY_2, NACK_KEY_3, NACK_KEY_4, NACK_KEY_5,
    NACK_KEY_6, NACK_KEY_7, NACK_KEY_8, NACK_KEY_9, NACK_KEY_0,

    NACK_KEY_ENTER = 40,
    NACK_KEY_ESCAPE = 41,
    NACK_KEY_BACKSPACE = 42,
    NACK_KEY_TAB = 43,
    NACK_KEY_SPACE = 44,
    NACK_KEY_MINUS = 45,
    NACK_KEY_EQUAL = 46,
    NACK_KEY_LEFT_BRACKET = 47,
    NACK_KEY_RIGHT_BRACKET = 48,
    NACK_KEY_BACKSLASH = 49,
    NACK_KEY_NON_US_HASH = 50,
    NACK_KEY_SEMICOLON = 51,
    NACK_KEY_APOSTROPHE = 52,
    NACK_KEY_GRAVE = 53,
    NACK_KEY_COMMA = 54,
    NACK_KEY_PERIOD = 55,
    NACK_KEY_SLASH = 56,
    NACK_KEY_CAPS_LOCK = 57,

    NACK_KEY_F1 = 58, NACK_KEY_F2, NACK_KEY_F3, NACK_KEY_F4, NACK_KEY_F5,
    NACK_KEY_F6, NACK_KEY_F7, NACK_KEY_F8, NACK_KEY_F9, NACK_KEY_F10,
    NACK_KEY_F11, NACK_KEY_F12,

    NACK_KEY_PRINT_SCREEN = 70,
    NACK_KEY_SCROLL_LOCK = 71,
    NACK_KEY_PAUSE = 72,
    NACK_KEY_INSERT = 73,
    NACK_KEY_HOME = 74,
    NACK_KEY_PAGE_UP = 75,
    NACK_KEY_DELETE = 76,
    NACK_KEY_END = 77,
    NACK_KEY_PAGE_DOWN = 78,
    NACK_KEY_RIGHT = 79,
    NACK_KEY_LEFT = 80,
    NACK_KEY_DOWN = 81,
    NACK_KEY_UP = 82,

    NACK_KEY_NUM_LOCK = 83,
    NACK_KEY_KP_DIVIDE = 84,
    NACK_KEY_KP_MULTIPLY = 85,
    NACK_KEY_KP_SUBTRACT = 86,
    NACK_KEY_KP_ADD = 87,
    NACK_KEY_KP_ENTER = 88,
    NACK_KEY_KP_1 = 89, NACK_KEY_KP_2, NACK_KEY_KP_3, NACK_KEY_KP_4,
    NACK_KEY_KP_5, NACK_KEY_KP_6, NACK_KEY_KP_7, NACK_KEY_KP_8, NACK_KEY_KP_9,
    NACK_KEY_KP_0 = 98,
    NACK_KEY_KP_DECIMAL = 99,
    NACK_KEY_NON_US_BACKSLASH = 100,
    NACK_KEY_APPLICATION = 101,
    NACK_KEY_KP_EQUAL = 103,

    NACK_KEY_F13 = 104, NACK_KEY_F14, NACK_KEY_F15, NACK_KEY_F16,
    NACK_KEY_F17, NACK_KEY_F18, NACK_KEY_F19, NACK_KEY_F20,
    NACK_KEY_F21, NACK_KEY_F22, NACK_KEY_F23, NACK_KEY_F24,

    NACK_KEY_MENU = 118,
    NACK_KEY_MUTE = 127,
    NACK_KEY_VOLUME_UP = 128,
    NACK_KEY_VOLUME_DOWN = 129,

    NACK_KEY_LEFT_CTRL = 224,
    NACK_KEY_LEFT_SHIFT = 225,
    NACK_KEY_LEFT_ALT = 226,
    NACK_KEY_LEFT_SUPER = 227,
    NACK_KEY_RIGHT_CTRL = 228,
    NACK_KEY_RIGHT_SHIFT = 229,
    NACK_KEY_RIGHT_ALT = 230,
    NACK_KEY_RIGHT_SUPER = 231,

    NACK_KEY_COUNT = 256
};

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

enum nack_gl_profile {
    NACK_GL_PROFILE_CORE = 0,
    NACK_GL_PROFILE_COMPAT,
    NACK_GL_PROFILE_ES
};

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

struct nack_window;
struct nack_gl_context;

struct nack_event {
    enum nack_event_type type;
    struct nack_window *window;
    uint64_t time_ns;
    /* Named rather than anonymous: anonymous members are C11, not C99. */
    union {
        struct { int width, height, fb_width, fb_height; } size;
        struct { int x, y; } move;
        struct { float scale; } scale;
        struct {
            enum nack_key key;
            uint32_t scancode;   /* raw platform scancode                    */
            uint32_t mods;
            bool repeat;
        } key;
        struct { char utf8[32]; } text;
        struct {
            double x, y;         /* in logical window coordinates            */
            double dx, dy;       /* delta since previous motion event        */
            uint32_t mods;
        } motion;
        struct {
            int button;
            double x, y;
            uint32_t mods;
            int click_count;     /* 1 = single, 2 = double, ...              */
        } button;
        struct {
            double dx, dy;       /* positive dy scrolls content up           */
            uint32_t mods;
            bool precise;        /* true for trackpads / high-res wheels     */
        } scroll;
    } data;
};

struct nack_init_desc {
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
struct nack_gl_desc {
    int major, minor;             /* 0 = "any", library picks a sane default */
    enum nack_gl_profile profile;
    bool debug;
    bool forward_compatible;
    bool robust;
    struct nack_gl_context *share;
};

/* -------------------------------------------------------------------------- */
/* Library lifetime                                                           */
/* -------------------------------------------------------------------------- */

NACK_API bool         nack_init(const struct nack_init_desc *desc);
NACK_API void         nack_shutdown(void);
NACK_API bool         nack_is_initialized(void);
NACK_API enum nack_backend nack_get_backend(void);
NACK_API const char  *nack_backend_name(enum nack_backend backend);

/* Last error for the calling thread; valid until the next failing call. */
NACK_API enum nack_result  nack_get_error(const char **message);

NACK_API void nack_window_desc_defaults(struct nack_window_desc *desc);
NACK_API void nack_gl_desc_defaults(struct nack_gl_desc *desc);
NACK_API void nack_framebuffer_desc_defaults(struct nack_framebuffer_desc *desc);

/* -------------------------------------------------------------------------- */
/* Windows                                                                    */
/* -------------------------------------------------------------------------- */

/*
 * Creates a window. A visible window appears on every backend as soon as it is
 * shown, even before anything has been drawn into it: on Wayland, where a
 * surface with no committed buffer would never be mapped at all, the library
 * commits a blank frame and retires it on the first nack_gl_swap_buffers.
 */
NACK_API struct nack_window *nack_window_create(const struct nack_window_desc *desc);
NACK_API void         nack_window_destroy(struct nack_window *window);

NACK_API void  nack_window_show(struct nack_window *window);
NACK_API void  nack_window_hide(struct nack_window *window);
NACK_API void  nack_window_focus(struct nack_window *window);
NACK_API void  nack_window_minimize(struct nack_window *window);
NACK_API void  nack_window_maximize(struct nack_window *window);
NACK_API void  nack_window_restore(struct nack_window *window);
NACK_API void  nack_window_request_attention(struct nack_window *window);

NACK_API void  nack_window_set_title(struct nack_window *window, const char *title);
NACK_API void  nack_window_set_size(struct nack_window *window, int width, int height);
NACK_API void  nack_window_get_size(const struct nack_window *window, int *width, int *height);
NACK_API void  nack_window_get_framebuffer_size(const struct nack_window *window, int *width, int *height);
NACK_API void  nack_window_get_position(const struct nack_window *window, int *x, int *y);
NACK_API void  nack_window_set_position(struct nack_window *window, int x, int y);
NACK_API float nack_window_get_content_scale(const struct nack_window *window);

NACK_API void  nack_window_set_size_limits(struct nack_window *window,
                                           int min_width, int min_height,
                                           int max_width, int max_height);
NACK_API void  nack_window_set_size_increments(struct nack_window *window, int dw, int dh);

NACK_API void  nack_window_set_fullscreen(struct nack_window *window, bool fullscreen);
NACK_API bool  nack_window_is_fullscreen(const struct nack_window *window);
NACK_API bool  nack_window_is_focused(const struct nack_window *window);
NACK_API bool  nack_window_is_minimized(const struct nack_window *window);
NACK_API bool  nack_window_is_maximized(const struct nack_window *window);

NACK_API bool  nack_window_should_close(const struct nack_window *window);
NACK_API void  nack_window_set_should_close(struct nack_window *window, bool value);

NACK_API void *nack_window_get_user_data(const struct nack_window *window);
NACK_API void  nack_window_set_user_data(struct nack_window *window, void *user_data);

NACK_API void  nack_window_set_cursor_shape(struct nack_window *window, enum nack_cursor_shape shape);
NACK_API void  nack_window_set_cursor_mode(struct nack_window *window, enum nack_cursor_mode mode);
NACK_API enum nack_cursor_mode nack_window_get_cursor_mode(const struct nack_window *window);

/* Marks the whole window as needing a repaint; produces a WINDOW_EXPOSE. */
NACK_API void  nack_window_request_redraw(struct nack_window *window);

/* -------------------------------------------------------------------------- */
/* Events                                                                     */
/* -------------------------------------------------------------------------- */

/* Non-blocking: returns false when the queue is empty. */
NACK_API bool nack_poll_event(struct nack_event *event);
/* Blocks until at least one event is available. */
NACK_API bool nack_wait_event(struct nack_event *event);
/* Blocks for at most `timeout` seconds; returns false on timeout. */
NACK_API bool nack_wait_event_timeout(struct nack_event *event, double timeout);
/* Thread-safe: unblocks a waiting nack_wait_event* and queues NACK_EVENT_WAKEUP. */
NACK_API void nack_wakeup(void);

/* Instantaneous input state, updated as events are generated. */
NACK_API bool     nack_key_is_down(enum nack_key key);
NACK_API uint32_t nack_get_mods(void);
NACK_API bool     nack_mouse_button_is_down(int button);
NACK_API void     nack_get_mouse_position(struct nack_window *window, double *x, double *y);

NACK_API const char *nack_key_get_name(enum nack_key key);

/* -------------------------------------------------------------------------- */
/* OpenGL                                                                     */
/* -------------------------------------------------------------------------- */

NACK_API struct nack_gl_context *nack_gl_context_create(struct nack_window *window, const struct nack_gl_desc *desc);
NACK_API void  nack_gl_context_destroy(struct nack_gl_context *context);
NACK_API bool  nack_gl_make_current(struct nack_window *window, struct nack_gl_context *context);
NACK_API void  nack_gl_swap_buffers(struct nack_window *window);
NACK_API void  nack_gl_set_swap_interval(int interval);
/*
 * Resolves an OpenGL entry point. Suitable as the loader callback for glad,
 * epoxy, or a hand-rolled loader; results are cached internally, so resolving
 * a few hundred names at startup is cheap and repeat loads are cheaper.
 *
 * A non-NULL result does NOT prove the function is usable: some drivers
 * (anything on libglvnd) return a dispatch stub for every gl-prefixed name.
 * Gate optional functionality on the context version or on
 * nack_gl_extension_supported(), never on the pointer alone.
 */
NACK_API void *nack_gl_get_proc_address(const char *name);
NACK_API bool  nack_gl_extension_supported(const char *name);

/* -------------------------------------------------------------------------- */
/* Clipboard                                                                  */
/* -------------------------------------------------------------------------- */

NACK_API bool        nack_clipboard_set_text(const char *utf8);
/* Returned string is owned by the library and valid until the next call. */
NACK_API const char *nack_clipboard_get_text(void);

/* Primary selection (X11/Wayland). Returns false/NULL elsewhere. */
NACK_API bool        nack_primary_set_text(const char *utf8);
NACK_API const char *nack_primary_get_text(void);

/* -------------------------------------------------------------------------- */
/* Time                                                                       */
/* -------------------------------------------------------------------------- */

NACK_API uint64_t nack_time_ns(void);
NACK_API double   nack_time_seconds(void);

/* -------------------------------------------------------------------------- */
/* Native handles (for interop; fields are backend-specific)                  */
/* -------------------------------------------------------------------------- */

struct nack_native_window {
    enum nack_backend backend;
    void *display;      /* Display* / wl_display* / NULL                     */
    void *surface;      /* wl_surface* / NSWindow* / HWND                    */
    uintptr_t handle;   /* X11 Window id / HWND / 0                          */
};

NACK_API void nack_window_get_native(const struct nack_window *window, struct nack_native_window *out);

#if defined(__cplusplus)
}
#endif

#endif /* NACK_H_INCLUDED */
