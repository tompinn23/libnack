/* Internal shared definitions for libnack backends. */
#ifndef NACK_INTERNAL_H_INCLUDED
#define NACK_INTERNAL_H_INCLUDED

#include "nack/nack.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define NACK_EVENT_QUEUE_CAP 512
#define NACK_MAX_WINDOWS 64

#if defined(_WIN32)
#  define NACK_PLATFORM_WIN32 1
#elif defined(__APPLE__)
#  define NACK_PLATFORM_COCOA 1
#else
#  define NACK_PLATFORM_UNIX 1
#endif

typedef struct nack_backend_vt nack_backend_vt;

struct nack_window {
    const nack_backend_vt *vt;
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
    nack_framebuffer_desc framebuffer;
    bool visible;
    bool focused;
    bool minimized;
    bool maximized;
    bool fullscreen;
    bool should_close;
    bool destroyed;

    nack_cursor_mode cursor_mode;
    nack_cursor_shape cursor_shape;

    double mouse_x, mouse_y;

    /* multi-click tracking */
    uint64_t last_click_time_ns;
    double last_click_x, last_click_y;
    int last_click_button;
    int click_count;

    void *user_data;
};

struct nack_gl_context {
    const nack_backend_vt *vt;
    void *native;
    nack_window *owner;   /* window whose pixel format the context matches */
};

struct nack_backend_vt {
    const char *name;
    nack_backend id;

    bool (*init)(const nack_init_desc *desc);
    void (*shutdown)(void);

    bool (*window_create)(nack_window *w, const nack_window_desc *desc);
    void (*window_destroy)(nack_window *w);
    void (*window_show)(nack_window *w, bool show);
    void (*window_focus)(nack_window *w);
    void (*window_set_title)(nack_window *w, const char *title);
    void (*window_set_size)(nack_window *w, int width, int height);
    void (*window_set_position)(nack_window *w, int x, int y);
    void (*window_apply_size_hints)(nack_window *w);
    void (*window_set_fullscreen)(nack_window *w, bool fullscreen);
    void (*window_minimize)(nack_window *w);
    void (*window_maximize)(nack_window *w);
    void (*window_restore)(nack_window *w);
    void (*window_request_attention)(nack_window *w);
    void (*window_request_redraw)(nack_window *w);
    void (*window_set_cursor_shape)(nack_window *w, nack_cursor_shape shape);
    void (*window_set_cursor_mode)(nack_window *w, nack_cursor_mode mode);
    void (*window_get_native)(const nack_window *w, nack_native_window *out);

    /* Pump platform events into the queue. timeout < 0 blocks indefinitely,
     * 0 polls, > 0 waits at most that many seconds. */
    void (*pump_events)(double timeout);
    void (*wakeup)(void);

    nack_gl_context *(*gl_create)(nack_window *w, const nack_gl_desc *desc);
    void  (*gl_destroy)(nack_gl_context *ctx);
    bool  (*gl_make_current)(nack_window *w, nack_gl_context *ctx);
    void  (*gl_swap_buffers)(nack_window *w);
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

typedef struct nack_state {
    bool initialized;
    const nack_backend_vt *vt;
    char *app_id;

    void (*log_fn)(const char *, void *);
    void *log_user_data;

    nack_event queue[NACK_EVENT_QUEUE_CAP];
    size_t queue_head, queue_tail;

    nack_window *windows[NACK_MAX_WINDOWS];
    size_t window_count;

    bool keys[NACK_KEY_COUNT];
    bool mouse_buttons[NACK_MOUSE_BUTTON_COUNT];
    uint32_t mods;

    nack_gl_context *current_context;
    nack_window *current_window;
    int swap_interval;

    nack_result error_code;
    char error_message[512];
} nack_state;

extern nack_state nack__g;

/* Diagnostics */
void nack__log(const char *fmt, ...);
bool nack__fail(nack_result code, const char *fmt, ...);

/* Event queue */
void nack__push_event(const nack_event *ev);
nack_event *nack__event_begin(nack_event_type type, nack_window *w);
bool nack__queue_empty(void);

/* Convenience emitters used by backends */
void nack__emit_resize(nack_window *w, int width, int height, int fb_width, int fb_height);
void nack__emit_key(nack_window *w, nack_key key, uint32_t scancode, uint32_t mods,
                    bool down, bool repeat);
void nack__emit_text(nack_window *w, const char *utf8);
void nack__emit_mouse_button(nack_window *w, int button, bool down, double x, double y,
                             uint32_t mods);
void nack__emit_mouse_move(nack_window *w, double x, double y, uint32_t mods);
void nack__emit_scroll(nack_window *w, double dx, double dy, uint32_t mods, bool precise);
void nack__emit_simple(nack_window *w, nack_event_type type);
void nack__emit_focus(nack_window *w, bool focused);
void nack__emit_scale(nack_window *w, float scale);

/* Window registry */
void nack__register_window(nack_window *w);
void nack__unregister_window(nack_window *w);

/* GL entry-point lookup cache (src/common/nack_proc_cache.c) */
void *nack__proc_cache_get(const char *name, void *(*resolve)(const char *));
void  nack__proc_cache_clear(void);

/* Helpers */
char *nack__strdup(const char *s);
void *nack__calloc(size_t count, size_t size);
uint32_t nack__utf8_encode(uint32_t codepoint, char out[static 5]);
bool nack__codepoint_is_text(uint32_t codepoint);

/* Backend registration - each platform provides the ones it supports. */
const nack_backend_vt *nack__backend_win32(void);
const nack_backend_vt *nack__backend_cocoa(void);
const nack_backend_vt *nack__backend_wayland(void);
const nack_backend_vt *nack__backend_x11(void);

#endif /* NACK_INTERNAL_H_INCLUDED */
