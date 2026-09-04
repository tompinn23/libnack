/*
 * Internal windowing and OpenGL context layer.
 *
 * This is no longer part of libnack's public API: the library exposes only the
 * cell console (include/nack/nack.hpp), and owns the window and GL context
 * behind it. Everything here is for the console renderer's use, and is C++ -
 * the one test still in C, the Win32 ABI check, never reaches in here.
 *
 * Backends: Win32, Cocoa, Wayland, X11.
 *
 * The API is deliberately shaped for the needs of a grid/terminal renderer:
 * blocking event waits with timeouts, thread-safe wakeups, size increments,
 * per-monitor DPI scaling, clipboard access and UTF-8 text input.
 *
 * Threading: unless documented otherwise, every function and method must be
 * called from the thread that called nack_state::init(). The exceptions are
 * nack_state::wakeup() and nack::detail::win_time_ns()/win_time_seconds(),
 * which are safe from any thread.
 */
#ifndef NACK_WINDOW_H_INCLUDED
#define NACK_WINDOW_H_INCLUDED

#include "nack_core.h"   /* keys, mods, mouse buttons, colours */
#include "nack_backend_id.h"

#include <cstddef>
#include <cstdint>

#ifndef __cplusplus
#  error "nack_window.h is C++"
#endif

/* -------------------------------------------------------------------------- */
/* Enumerations                                                               */
/* -------------------------------------------------------------------------- */

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

    NACK_WIN_EVENT_WAKEUP,             /* produced by nack_state::wakeup()            */
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

namespace nack { namespace detail {

enum gl_profile {
    NACK__GL_PROFILE_CORE = 0,
    NACK__GL_PROFILE_COMPAT,
    NACK__GL_PROFILE_ES
};

} }   /* namespace nack::detail */

/*
 * Every nack__-prefixed name below this point lives in nack::detail - a real
 * C++ namespace rather than a name-mangled-by-hand prefix - and is brought in
 * unqualified from here on by this using-directive. Fine for an
 * implementation header nothing outside the library includes; a public one
 * would never do this.
 */
using namespace nack::detail;

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

struct nack_window;
struct nack_gl_context;

/*
 * Window-level input, in pixels. The console layer turns this into the public
 * cell-based nack_event.
 */
struct nack_win_event {
    nack_win_event_type type;
    nack_window *window;
    uint64_t time_ns;
    union {
        struct { int width, height, fb_width, fb_height; } size;
        struct { int x, y; } move;
        struct { float scale; } scale;
        struct {
            nack_key key;
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
    nack_backend backend = NACK_BACKEND_NONE;
    /* Application identifier: Wayland xdg app-id and X11 WM_CLASS. */
    const char *app_id = nullptr;
    /* Optional diagnostic sink; receives NUL-terminated messages. */
    void (*log_fn)(const char *message, void *user_data) = nullptr;
    void *log_user_data = nullptr;
};

/*
 * Framebuffer format. This is a property of the window, not of the context:
 * on EGL and Wayland the window's visual is derived from the pixel format, so
 * it has to be fixed when the window is created.
 */
struct nack_framebuffer_desc {
    int red_bits = 8, green_bits = 8, blue_bits = 8, alpha_bits = 0;
    int depth_bits = 24, stencil_bits = 8;
    int samples = 0;              /* MSAA sample count, 0 = off              */
    bool srgb = false;
    bool double_buffer = true;
};

struct nack_window_desc {
    const char *title = "libnack";
    int width = 800, height = 600;  /* logical pixels; <= 0 picks a default  */
    int min_width = 0, min_height = 0;    /* 0 = unconstrained                */
    int max_width = 0, max_height = 0;    /* 0 = unconstrained                */
    int width_increment = 0;      /* 0 = none (struct cell-size snapping)    */
    int height_increment = 0;
    bool resizable = true;
    bool decorated = true;
    bool visible = true;
    bool fullscreen = false;
    bool maximized = false;
    bool transparent = false;
    bool high_dpi = true;         /* opt in to a framebuffer at native DPI   */
    nack_framebuffer_desc framebuffer;
    void *user_data = nullptr;
};

namespace nack { namespace detail {

/* Context attributes. The framebuffer format comes from the window. */
struct gl_desc {
    int major = 3, minor = 3;     /* 0 = "any", library picks a sane default */
    gl_profile profile = NACK__GL_PROFILE_CORE;
    bool debug = false;
    bool forward_compatible = false;
    bool robust = false;
    nack_gl_context *share = nullptr;
};

} }   /* namespace nack::detail */

/* -------------------------------------------------------------------------- */
/* Free-standing utilities                                                    */
/*                                                                             */
/* Everything else - window lifetime, events, GL, clipboard - is a method on  */
/* nack_window or nack_state, declared in nack_internal.h. What */
/* is left here has no natural receiver: a backend-name lookup, a key-name    */
/* lookup, and the OS clock, none of which touch any library state.          */
/* -------------------------------------------------------------------------- */

namespace nack { namespace detail {

const char *win_backend_name(nack_backend backend);

/* Safe from any thread, unlike everything that touches nack_state. */
uint64_t win_time_ns(void);
double   win_time_seconds(void);

} }   /* namespace nack::detail */

const char *nack_key_get_name(nack_key key);

/* -------------------------------------------------------------------------- */
/* Native handles (for interop; fields are backend-specific)                  */
/* -------------------------------------------------------------------------- */

struct nack_native_window {
    nack_backend backend;
    void *display;      /* Display* / wl_display* / NULL                     */
    void *surface;      /* wl_surface* / NSWindow* / HWND                    */
    void *view;         /* NSView*, which is what a CAMetalLayer attaches to */
    uintptr_t handle;   /* X11 Window id / HWND / 0                          */
};

#endif /* NACK_WINDOW_H_INCLUDED */
