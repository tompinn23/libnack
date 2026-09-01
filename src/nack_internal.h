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

#include "nack/nack.h"
#include "nack_window.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define NACK_WIN_EVENT_QUEUE_CAP 512
#define NACK_MAX_WINDOWS 64

#if defined(_WIN32)
#  define NACK_PLATFORM_WIN32 1
#elif defined(__APPLE__)
#  define NACK_PLATFORM_COCOA 1
#else
#  define NACK_PLATFORM_UNIX 1
#endif

struct nack_backend_vt;

struct nack_window {
    const struct nack_backend_vt *vt;
    void *native;              /* backend-owned per-window state */

    char *title;
    int width, height;         /* logical size                    */
    int fb_width, fb_height;   /* framebuffer (physical) size      */
    int pos_x, pos_y;
    float scale;

    int min_width, min_height;
    int max_width, max_height;
    int inc_width, inc_height;

    bool resizable;
    bool decorated;
    bool transparent;
    bool high_dpi;
    struct nack_framebuffer_desc framebuffer;
    bool visible;
    bool focused;
    bool minimized;
    bool maximized;
    bool fullscreen;
    bool should_close;
    bool destroyed;

    enum nack_cursor_mode cursor_mode;
    enum nack_cursor_shape cursor_shape;

    double mouse_x, mouse_y;

    /* multi-click tracking */
    uint64_t last_click_time_ns;
    double last_click_x, last_click_y;
    int last_click_button;
    int click_count;

    void *user_data;
};

struct nack_gl_context {
    const struct nack_backend_vt *vt;
    void *native;
    struct nack_window *owner;   /* window whose pixel format the context matches */
};

struct nack_backend_vt {
    const char *name;
    enum nack_backend id;

    bool (*init)(const struct nack_win_init_desc *desc);
    void (*shutdown)(void);

    bool (*window_create)(struct nack_window *w, const struct nack_window_desc *desc);
    void (*window_destroy)(struct nack_window *w);
    void (*window_show)(struct nack_window *w, bool show);
    void (*window_focus)(struct nack_window *w);
    void (*window_set_title)(struct nack_window *w, const char *title);
    void (*window_set_size)(struct nack_window *w, int width, int height);
    void (*window_set_position)(struct nack_window *w, int x, int y);
    void (*window_apply_size_hints)(struct nack_window *w);
    void (*window_set_fullscreen)(struct nack_window *w, bool fullscreen);
    void (*window_minimize)(struct nack_window *w);
    void (*window_maximize)(struct nack_window *w);
    void (*window_restore)(struct nack_window *w);
    void (*window_request_attention)(struct nack_window *w);
    void (*window_request_redraw)(struct nack_window *w);
    void (*window_set_cursor_shape)(struct nack_window *w, enum nack_cursor_shape shape);
    void (*window_set_cursor_mode)(struct nack_window *w, enum nack_cursor_mode mode);
    void (*window_get_native)(const struct nack_window *w, struct nack_native_window *out);

    /* Pump platform events into the queue. timeout < 0 blocks indefinitely,
     * 0 polls, > 0 waits at most that many seconds. */
    void (*pump_events)(double timeout);
    void (*wakeup)(void);

    struct nack_gl_context *(*gl_create)(struct nack_window *w, const struct nack__gl_desc *desc);
    void  (*gl_destroy)(struct nack_gl_context *ctx);
    bool  (*gl_make_current)(struct nack_window *w, struct nack_gl_context *ctx);
    void  (*gl_swap_buffers)(struct nack_window *w);
    void  (*gl_set_swap_interval)(int interval);
    void *(*gl_get_proc_address)(const char *name);

    bool         (*clipboard_set)(const char *utf8);
    const char  *(*clipboard_get)(void);
    bool         (*primary_set)(const char *utf8);
    const char  *(*primary_get)(void);
};

/* ------------------------------------------------------------------ */
/* Shared state, defined in nack.c                                    */
/* ------------------------------------------------------------------ */

struct nack_state {
    bool initialized;
    const struct nack_backend_vt *vt;
    char *app_id;

    void (*log_fn)(const char *, void *);
    void *log_user_data;

    struct nack_win_event queue[NACK_WIN_EVENT_QUEUE_CAP];
    size_t queue_head, queue_tail;

    struct nack_window *windows[NACK_MAX_WINDOWS];
    size_t window_count;

    bool keys[NACK_KEY_COUNT];
    bool mouse_buttons[NACK_MOUSE_BUTTON_COUNT];
    uint32_t mods;

    struct nack_gl_context *current_context;
    struct nack_window *current_window;
    int swap_interval;

    enum nack_result error_code;
    char error_message[512];
};

extern struct nack_state nack__g;

/* Diagnostics */
void nack__log(const char *fmt, ...);
bool nack__fail(enum nack_result code, const char *fmt, ...);

/* Event queue */
void nack__push_event(const struct nack_win_event *ev);
struct nack_win_event *nack__event_begin(enum nack_win_event_type type, struct nack_window *w);
bool nack__queue_empty(void);

/* Convenience emitters used by backends */
void nack__emit_resize(struct nack_window *w, int width, int height, int fb_width,
                       int fb_height);
void nack__emit_key(struct nack_window *w, enum nack_key key, uint32_t scancode, uint32_t mods,
                    bool down, bool repeat);
void nack__emit_text(struct nack_window *w, const char *utf8);
void nack__emit_mouse_button(struct nack_window *w, int button, bool down, double x, double y,
                             uint32_t mods);
void nack__emit_mouse_move(struct nack_window *w, double x, double y, uint32_t mods);
void nack__emit_scroll(struct nack_window *w, double dx, double dy, uint32_t mods,
                       bool precise);
void nack__emit_simple(struct nack_window *w, enum nack_win_event_type type);
void nack__emit_focus(struct nack_window *w, bool focused);
void nack__emit_scale(struct nack_window *w, float scale);

/* Window registry */
void nack__register_window(struct nack_window *w);
void nack__unregister_window(struct nack_window *w);

/* GL entry-point lookup cache (src/common/nack_proc_cache.c) */
void *nack__proc_cache_get(const char *name, void *(*resolve)(const char *));
void  nack__proc_cache_clear(void);

/* Helpers */
char *nack__strdup(const char *s);
void *nack__calloc(size_t count, size_t size);
/* out must have room for 4 bytes plus a terminator. Spelled [5] rather
 * than the C99 [static 5], which MSVC does not implement. */
uint32_t nack__utf8_encode(uint32_t codepoint, char out[5]);
bool nack__codepoint_is_text(uint32_t codepoint);

/* Backend registration - each platform provides the ones it supports. */
const struct nack_backend_vt *nack__backend_win32(void);
const struct nack_backend_vt *nack__backend_cocoa(void);
const struct nack_backend_vt *nack__backend_wayland(void);
const struct nack_backend_vt *nack__backend_x11(void);

#endif /* NACK_INTERNAL_H_INCLUDED */
