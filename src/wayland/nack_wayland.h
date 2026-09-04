#ifndef NACK_WAYLAND_H_INCLUDED
#define NACK_WAYLAND_H_INCLUDED

#include "../nack_internal.h"
#include <optional>
#include <string>
#include "../common/nack_egl.h"

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "primary-selection-unstable-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#if defined(NACK_HAS_FRACTIONAL_SCALE)
#  include "fractional-scale-v1-client-protocol.h"
#endif

#define NACK_WL_MAX_OUTPUTS 16

struct nack_wl_output {
    struct wl_output *output;
    uint32_t name;
    int32_t scale;
    int32_t width, height;
    int32_t physical_width, physical_height;
};

/* Fallback client-side decorations, used when the compositor has no
 * xdg-decoration support (Mutter, notably) or asks the client to draw. */
enum nack_wl_decor_part {
    NACK_WL_DECOR_TOP = 0,     /* title bar */
    NACK_WL_DECOR_BOTTOM,
    NACK_WL_DECOR_LEFT,
    NACK_WL_DECOR_RIGHT,
    NACK_WL_DECOR_COUNT
};

/* Title bar buttons, laid out right aligned in this order. */
enum nack_wl_decor_button {
    NACK_WL_BUTTON_NONE = -1,
    NACK_WL_BUTTON_MINIMIZE = 0,
    NACK_WL_BUTTON_MAXIMIZE,
    NACK_WL_BUTTON_CLOSE,
    NACK_WL_BUTTON_COUNT
};

/*
 * A wl_shm buffer and its mapping. Used for the decoration surfaces and for
 * the placeholder frame that maps a window before the client has drawn.
 */
struct nack_wl_shm_buffer {
    struct wl_buffer *buffer;
    uint32_t *pixels;
    size_t size;
    int width, height;          /* device pixels */
};

struct nack_wl_decor {
    struct wl_surface *surface;
    struct wl_subsurface *subsurface;
    nack_wl_shm_buffer buf;
    int width, height;          /* logical pixels */
};

struct nack_wl_window {
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct zxdg_toplevel_decoration_v1 *decoration;
    struct wl_egl_window *egl_window;
    EGLSurface egl_surface;
    EGLConfig config;
    bool has_config;

    struct wp_viewport *viewport;
#if defined(NACK_HAS_FRACTIONAL_SCALE)
    struct wp_fractional_scale_v1 *fractional_scale;
#endif
    struct zwp_locked_pointer_v1 *locked_pointer;
    struct zwp_relative_pointer_v1 *relative_pointer;

    /* Outputs this surface currently overlaps, for integer scale selection. */
    struct wl_output *outputs[NACK_WL_MAX_OUTPUTS];
    size_t output_count;
    int32_t buffer_scale;

    /* Size negotiated by the compositor in the current configure sequence. */
    int32_t pending_width, pending_height;
    bool pending_maximized, pending_fullscreen, pending_activated;
    bool configured;

    double virtual_x, virtual_y;

    nack_wl_decor decor[NACK_WL_DECOR_COUNT];
    bool client_side_decorations;
    nack_wl_decor_button decor_hover;
    int decor_scale;            /* integer scale the decorations render at */

    /*
     * Wayland maps a surface only once a buffer has been committed to it, so
     * a window that has not drawn yet is invisible rather than merely empty.
     * This blank frame is committed when the window is shown and dropped as
     * soon as the client presents its own.
     */
    nack_wl_shm_buffer placeholder;
    bool presented;
};

struct nack_wayland_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct xdg_wm_base *wm_base;
    struct zxdg_decoration_manager_v1 *decoration_manager;
    struct wp_viewporter *viewporter;
#if defined(NACK_HAS_FRACTIONAL_SCALE)
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
#endif
    struct zwp_pointer_constraints_v1 *pointer_constraints;
    struct zwp_relative_pointer_manager_v1 *relative_pointer_manager;

    struct wl_data_device_manager *data_device_manager;
    struct wl_data_device *data_device;
    struct zwp_primary_selection_device_manager_v1 *primary_manager;
    struct zwp_primary_selection_device_v1 *primary_device;

    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct wl_touch *touch;

    nack_wl_output outputs[NACK_WL_MAX_OUTPUTS];
    size_t output_count;

    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;
    struct wl_cursor *cursors[NACK_CURSOR_SHAPE_COUNT];
    bool cursors_loaded[NACK_CURSOR_SHAPE_COUNT];
    int cursor_theme_scale;

    /* Input bookkeeping */
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    struct xkb_compose_table *compose_table;
    struct xkb_compose_state *compose_state;
    nack_key keycodes[256];

    nack_window *pointer_focus;
    /* Set when the pointer is over one of a window's decoration surfaces. */
    nack_window *decor_focus;
    nack_wl_decor_part decor_focus_part;
    double decor_x, decor_y;
    nack_window *keyboard_focus;
    uint32_t pointer_enter_serial;
    uint32_t last_serial;
    double pointer_x, pointer_y;

    /* Key repeat, which Wayland leaves to the client. */
    int32_t repeat_rate, repeat_delay;
    uint32_t repeat_key;          /* raw wayland key code, 0 = idle */
    uint64_t repeat_next_ns;
    nack_window *repeat_window;

    /* Accumulated axis events for the current wl_pointer frame. */
    double axis_x, axis_y;
    bool axis_discrete;
    bool axis_pending;

    int wakeup_pipe[2];

    std::optional<std::string> clipboard_text;
    std::optional<std::string> primary_text;
    std::optional<std::string> clipboard_offered;
    std::optional<std::string> primary_offered;
    struct wl_data_source *data_source;
    struct zwp_primary_selection_source_v1 *primary_source;
    struct wl_data_offer *pending_offer;
    struct zwp_primary_selection_offer_v1 *pending_primary_offer;
    bool offer_has_text;
    bool primary_offer_has_text;
};

extern nack_wayland_state nack__wl;

static inline nack_wl_window *nack__wl_win(nack_window *w)
{
    return (nack_wl_window *)w->native;
}

/* nack_wayland_input.c */
void nack__wl_seat_bind(uint32_t name, uint32_t version);
void nack__wl_seat_release(void);
void nack__wl_input_shutdown(void);
void nack__wl_pump_key_repeat(void);
double nack__wl_next_repeat_timeout(void);
void nack__wl_set_cursor_shape(nack_window *w, nack_cursor_shape shape);
/* Sets the pointer image directly, regardless of which window has focus; the
 * decorations use it to show resize cursors while over a border. */
void nack__wl_apply_cursor_shape(nack_cursor_shape shape);
void nack__wl_set_cursor_mode(nack_window *w, nack_cursor_mode mode);
void nack__wl_update_cursor(nack_window *w);
void nack__wl_load_cursor_theme(int scale);

/* nack_wayland_clipboard.c */
void nack__wl_data_device_bind(void);
void nack__wl_clipboard_shutdown(void);
bool nack__wl_clipboard_set(const char *utf8);
const char *nack__wl_clipboard_get(void);
bool nack__wl_primary_set(const char *utf8);
const char *nack__wl_primary_get(void);

/* nack_wayland_decor.c */
bool nack__wl_shm_buffer_alloc(nack_wl_shm_buffer *buf, int width,
                               int height);
void nack__wl_shm_buffer_release(nack_wl_shm_buffer *buf);

/* Commits a blank frame so the surface maps before the client has drawn. */
void nack__wl_present_placeholder(nack_window *w);
void nack__wl_drop_placeholder(nack_window *w);

bool nack__wl_decor_enable(nack_window *w);
void nack__wl_decor_destroy(nack_window *w);
void nack__wl_decor_resize(nack_window *w);
void nack__wl_decor_redraw(nack_window *w);
/* Returns true when the surface belongs to this window's decorations. */
bool nack__wl_decor_find(struct wl_surface *surface, nack_window **out_window,
                         nack_wl_decor_part *out_part);
void nack__wl_decor_pointer_motion(nack_window *w, double x, double y);
bool nack__wl_decor_pointer_button(nack_window *w, int button, bool down,
                                   uint32_t serial);
void nack__wl_decor_pointer_leave(nack_window *w);
/* Recomputes the decoration scale after a DPI change. */
void nack__wl_decor_update_scale(nack_window *w, int scale);
int  nack__wl_decor_titlebar_height(const nack_window *w);
int  nack__wl_decor_border(const nack_window *w);

/* nack_wayland.c */
void nack__wl_window_update_scale(nack_window *w);
void nack__wl_resize_egl(nack_window *w);

#endif /* NACK_WAYLAND_H_INCLUDED */
