#ifndef NACK_XCB_H_INCLUDED
#define NACK_XCB_H_INCLUDED

#include "../nack_internal.h"
#include <optional>
#include <string>
#include "../common/nack_egl.h"

#include <xcb/xcb.h>
/*
 * xcb/xkb.h names two struct members `explicit`, which is a keyword in C++.
 * Renaming it around the include is the usual workaround and is safe here
 * because nothing in libnack touches those members - they belong to
 * xcb_xkb_get_map_map_t and the set-explicit request, neither of which this
 * backend uses. If that ever changes, the field is spelled explicit_.
 */
#define explicit nack__xcb_explicit
#include <xcb/xkb.h>
#undef explicit
#include <xcb/xcb_cursor.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon-compose.h>

struct nack_xcb_window {
    xcb_window_t handle;
    xcb_colormap_t colormap;
    EGLSurface surface;
    EGLConfig config;
    bool has_config;
    xcb_window_t egl_native;    /* stable storage for the XCB platform path */
    int restore_x, restore_y, restore_w, restore_h;
    bool warp_pending;
    double virtual_x, virtual_y;
};

struct nack_xcb_atoms {
    xcb_atom_t WM_PROTOCOLS;
    xcb_atom_t WM_DELETE_WINDOW;
    xcb_atom_t WM_STATE;
    xcb_atom_t WM_NORMAL_HINTS;
    xcb_atom_t WM_CHANGE_STATE;
    xcb_atom_t NET_WM_PING;
    xcb_atom_t NET_WM_NAME;
    xcb_atom_t NET_WM_ICON_NAME;
    xcb_atom_t NET_WM_PID;
    xcb_atom_t NET_WM_STATE;
    xcb_atom_t NET_WM_STATE_FULLSCREEN;
    xcb_atom_t NET_WM_STATE_MAXIMIZED_VERT;
    xcb_atom_t NET_WM_STATE_MAXIMIZED_HORZ;
    xcb_atom_t NET_WM_STATE_HIDDEN;
    xcb_atom_t NET_WM_STATE_DEMANDS_ATTENTION;
    xcb_atom_t NET_ACTIVE_WINDOW;
    xcb_atom_t NET_SUPPORTED;
    xcb_atom_t MOTIF_WM_HINTS;
    xcb_atom_t UTF8_STRING;
    xcb_atom_t ATOM_PAIR;
    xcb_atom_t CLIPBOARD;
    xcb_atom_t TARGETS;
    xcb_atom_t MULTIPLE;
    xcb_atom_t INCR;
    xcb_atom_t TEXT_PLAIN_UTF8;
    xcb_atom_t NACK_SELECTION;
};

struct nack_xcb_state {
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    int screen_number;
    xcb_window_t root;
    xcb_window_t helper;          /* owns selections, never mapped */
    nack_xcb_atoms atom;
    float scale;

    /* Only non-NULL when the EGL implementation lacks EGL_EXT_platform_xcb
     * and we had to go through Xlib to get a Display* for EGL. */
    void *xlib_display;

    int wakeup_pipe[2];

    /* xkbcommon */
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    struct xkb_compose_table *compose_table;
    struct xkb_compose_state *compose_state;
    int32_t xkb_device_id;
    uint8_t xkb_event_base;
    bool xkb_available;

    nack_key keycodes[256];

    xcb_cursor_context_t *cursor_context;
    xcb_cursor_t cursors[NACK_CURSOR_SHAPE_COUNT];
    bool cursors_loaded[NACK_CURSOR_SHAPE_COUNT];
    xcb_cursor_t blank_cursor;

    std::optional<std::string> clipboard_owned;
    std::optional<std::string> primary_owned;
    std::optional<std::string> clipboard_received;
    std::optional<std::string> primary_received;
};

extern nack_xcb_state nack__xcb;

static inline nack_xcb_window *nack__xcb_win(nack_window *w)
{
    return (nack_xcb_window *)w->native;
}

/* Selection handling lives in nack_xcb_clipboard.c. */
bool  nack__xcb_handle_selection_request(xcb_selection_request_event_t *event);
void  nack__xcb_handle_selection_clear(xcb_selection_clear_event_t *event);
/* Serves the next chunk of an outgoing INCR transfer. False if the event was
 * not part of one, so the caller can carry on handling it. */
bool  nack__xcb_handle_property_notify(xcb_property_notify_event_t *event);
bool  nack__xcb_clipboard_set(const char *utf8);
const char *nack__xcb_clipboard_get(void);
bool  nack__xcb_primary_set(const char *utf8);
const char *nack__xcb_primary_get(void);
void  nack__xcb_clipboard_shutdown(void);

/* Dispatch a single event; shared with the nested loop the selection code
 * runs while waiting for a transfer to complete. */
void nack__xcb_dispatch(xcb_generic_event_t *event);

#endif /* NACK_XCB_H_INCLUDED */
