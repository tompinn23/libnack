/* libnack - XCB backend. */
#include "nack_xcb.h"
#include "../common/nack_xkb_keys.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#if defined(NACK_XCB_XLIB_FALLBACK)
#  include <X11/Xlib.h>
#  include <X11/Xlib-xcb.h>
#endif

nack_xcb_state nack__xcb;

/* ICCCM WM_NORMAL_HINTS flag bits (ICCCM 4.1.2.3). */
#define NACK_HINT_US_POSITION  (1u << 0)
#define NACK_HINT_US_SIZE      (1u << 1)
#define NACK_HINT_P_POSITION   (1u << 2)
#define NACK_HINT_P_SIZE       (1u << 3)
#define NACK_HINT_P_MIN_SIZE   (1u << 4)
#define NACK_HINT_P_MAX_SIZE   (1u << 5)
#define NACK_HINT_P_RESIZE_INC (1u << 6)
#define NACK_HINT_P_ASPECT     (1u << 7)
#define NACK_HINT_P_BASE_SIZE  (1u << 8)
#define NACK_HINT_P_WIN_GRAVITY (1u << 9)

#define NACK_NET_WM_STATE_REMOVE 0
#define NACK_NET_WM_STATE_ADD    1

static uint32_t nack__xcb_mods_from_state(void)
{
    if (!nack__xcb.xkb_state)
        return 0;
    struct xkb_state *state = nack__xcb.xkb_state;
    uint32_t mods = 0;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_SHIFT;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_CTRL;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_ALT;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_SUPER;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_CAPSLOCK;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_NUM,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_NUMLOCK;
    return mods;
}

/* ------------------------------------------------------------------ */
/* xkbcommon setup                                                    */
/* ------------------------------------------------------------------ */

static void nack__xcb_release_keymap(void)
{
    if (nack__xcb.xkb_state) {
        xkb_state_unref(nack__xcb.xkb_state);
        nack__xcb.xkb_state = NULL;
    }
    if (nack__xcb.xkb_keymap) {
        xkb_keymap_unref(nack__xcb.xkb_keymap);
        nack__xcb.xkb_keymap = NULL;
    }
}

static bool nack__xcb_load_keymap(void)
{
    nack__xcb_release_keymap();

    nack__xcb.xkb_keymap = xkb_x11_keymap_new_from_device(
        nack__xcb.xkb_context, nack__xcb.connection, nack__xcb.xkb_device_id,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!nack__xcb.xkb_keymap)
        return false;

    nack__xcb.xkb_state = xkb_x11_state_new_from_device(
        nack__xcb.xkb_keymap, nack__xcb.connection, nack__xcb.xkb_device_id);
    if (!nack__xcb.xkb_state) {
        nack__xcb_release_keymap();
        return false;
    }

    nack__xkb_build_keycodes(nack__xcb.xkb_keymap, nack__xcb.keycodes);
    return true;
}

static bool nack__xcb_init_xkb(void)
{
    nack__xcb.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!nack__xcb.xkb_context)
        return nack__fail(NACK_ERROR_PLATFORM, "xkb_context_new failed");

    uint16_t major = XKB_X11_MIN_MAJOR_XKB_VERSION;
    uint16_t minor = XKB_X11_MIN_MINOR_XKB_VERSION;
    if (!xkb_x11_setup_xkb_extension(nack__xcb.connection, major, minor,
                                     XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                     NULL, NULL, &nack__xcb.xkb_event_base, NULL))
        return nack__fail(NACK_ERROR_PLATFORM, "server has no usable XKB extension");

    nack__xcb.xkb_device_id = xkb_x11_get_core_keyboard_device_id(nack__xcb.connection);
    if (nack__xcb.xkb_device_id == -1)
        return nack__fail(NACK_ERROR_PLATFORM, "no core XKB keyboard device");

    if (!nack__xcb_load_keymap())
        return nack__fail(NACK_ERROR_PLATFORM, "failed to build XKB keymap");

    /* Ask for map and state changes so the keymap tracks layout switches. */
    const uint16_t required =
        XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS |
        XCB_XKB_MAP_PART_MODIFIER_MAP | XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
        XCB_XKB_MAP_PART_KEY_ACTIONS | XCB_XKB_MAP_PART_VIRTUAL_MODS |
        XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;
    const uint16_t events =
        XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
        XCB_XKB_EVENT_TYPE_STATE_NOTIFY;

    xcb_xkb_select_events(nack__xcb.connection, (xcb_xkb_device_spec_t)nack__xcb.xkb_device_id,
                          events, 0, events, required, required, NULL);

    /* Detectable auto-repeat turns held keys into press/press instead of the
     * press/release/press pairs a terminal would have to filter itself. */
    xcb_xkb_per_client_flags(nack__xcb.connection,
                             (xcb_xkb_device_spec_t)nack__xcb.xkb_device_id,
                             XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                             XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                             0, 0, 0);

    /* Compose sequences (dead keys, Ctrl+Shift+U) come from the locale. */
    const char *locale = getenv("LC_ALL");
    if (!locale || !*locale) locale = getenv("LC_CTYPE");
    if (!locale || !*locale) locale = getenv("LANG");
    if (!locale || !*locale) locale = "C";

    nack__xcb.compose_table = xkb_compose_table_new_from_locale(
        nack__xcb.xkb_context, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (nack__xcb.compose_table)
        nack__xcb.compose_state = xkb_compose_state_new(nack__xcb.compose_table,
                                                        XKB_COMPOSE_STATE_NO_FLAGS);

    nack__xcb.xkb_available = true;
    return true;
}

/* ------------------------------------------------------------------ */
/* Atoms                                                              */
/* ------------------------------------------------------------------ */

static xcb_atom_t nack__xcb_intern(const char *name)
{
    xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(nack__xcb.connection, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(nack__xcb.connection, cookie, NULL);
    xcb_atom_t atom = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

static void nack__xcb_intern_atoms(void)
{
#define NACK_ATOM(field, name) nack__xcb.atom.field = nack__xcb_intern(name)
    NACK_ATOM(WM_PROTOCOLS, "WM_PROTOCOLS");
    NACK_ATOM(WM_DELETE_WINDOW, "WM_DELETE_WINDOW");
    NACK_ATOM(WM_STATE, "WM_STATE");
    NACK_ATOM(WM_CHANGE_STATE, "WM_CHANGE_STATE");
    NACK_ATOM(NET_WM_PING, "_NET_WM_PING");
    NACK_ATOM(NET_WM_NAME, "_NET_WM_NAME");
    NACK_ATOM(NET_WM_ICON_NAME, "_NET_WM_ICON_NAME");
    NACK_ATOM(NET_WM_PID, "_NET_WM_PID");
    NACK_ATOM(NET_WM_STATE, "_NET_WM_STATE");
    NACK_ATOM(NET_WM_STATE_FULLSCREEN, "_NET_WM_STATE_FULLSCREEN");
    NACK_ATOM(NET_WM_STATE_MAXIMIZED_VERT, "_NET_WM_STATE_MAXIMIZED_VERT");
    NACK_ATOM(NET_WM_STATE_MAXIMIZED_HORZ, "_NET_WM_STATE_MAXIMIZED_HORZ");
    NACK_ATOM(NET_WM_STATE_HIDDEN, "_NET_WM_STATE_HIDDEN");
    NACK_ATOM(NET_WM_STATE_DEMANDS_ATTENTION, "_NET_WM_STATE_DEMANDS_ATTENTION");
    NACK_ATOM(NET_ACTIVE_WINDOW, "_NET_ACTIVE_WINDOW");
    NACK_ATOM(NET_SUPPORTED, "_NET_SUPPORTED");
    NACK_ATOM(MOTIF_WM_HINTS, "_MOTIF_WM_HINTS");
    NACK_ATOM(UTF8_STRING, "UTF8_STRING");
    NACK_ATOM(ATOM_PAIR, "ATOM_PAIR");
    NACK_ATOM(CLIPBOARD, "CLIPBOARD");
    NACK_ATOM(TARGETS, "TARGETS");
    NACK_ATOM(MULTIPLE, "MULTIPLE");
    NACK_ATOM(INCR, "INCR");
    NACK_ATOM(TEXT_PLAIN_UTF8, "text/plain;charset=utf-8");
    NACK_ATOM(NACK_SELECTION, "NACK_SELECTION");
#undef NACK_ATOM
    nack__xcb.atom.WM_NORMAL_HINTS = XCB_ATOM_WM_NORMAL_HINTS;
}

static bool nack__xcb_wm_supports(xcb_atom_t atom)
{
    xcb_get_property_cookie_t cookie =
        xcb_get_property(nack__xcb.connection, 0, nack__xcb.root,
                         nack__xcb.atom.NET_SUPPORTED, XCB_ATOM_ATOM, 0, 1024);
    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(nack__xcb.connection, cookie, NULL);
    if (!reply)
        return false;
    bool found = false;
    xcb_atom_t *atoms = (xcb_atom_t *)xcb_get_property_value(reply);
    int count = xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t);
    for (int i = 0; i < count; ++i) {
        if (atoms[i] == atom) { found = true; break; }
    }
    free(reply);
    return found;
}

/* ------------------------------------------------------------------ */
/* Window lookup                                                      */
/* ------------------------------------------------------------------ */

static nack_window *nack__xcb_lookup(xcb_window_t handle)
{
    for (size_t i = 0; i < nack__g.window_count; ++i) {
        nack_window *w = nack__g.windows[i];
        nack_xcb_window *xw = (nack_xcb_window *)w->native;
        if (xw && xw->handle == handle)
            return w;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Content scale                                                      */
/* ------------------------------------------------------------------ */

/* Reads Xft.dpi out of the root window's RESOURCE_MANAGER property; that is
 * where every desktop environment records the user's scaling choice. */
static float nack__xcb_query_scale(void)
{
    const char *env = getenv("NACK_SCALE");
    if (env && *env) {
        float scale = (float)atof(env);
        if (scale > 0.0f)
            return scale;
    }

    float dpi = 0.0f;
    xcb_get_property_cookie_t cookie =
        xcb_get_property(nack__xcb.connection, 0, nack__xcb.root,
                         XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 0, 16 * 1024);
    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(nack__xcb.connection, cookie, NULL);
    if (reply) {
        int len = xcb_get_property_value_length(reply);
        const char *data = (const char *)xcb_get_property_value(reply);
        for (int i = 0; i + 8 < len; ++i) {
            if (strncmp(data + i, "Xft.dpi:", 8) != 0)
                continue;
            if (i > 0 && data[i - 1] != '\n')
                continue;
            const char *p = data + i + 8;
            while (p < data + len && (*p == ' ' || *p == '\t'))
                ++p;
            char buffer[32];
            int n = 0;
            while (p < data + len && n < (int)sizeof buffer - 1 &&
                   ((*p >= '0' && *p <= '9') || *p == '.'))
                buffer[n++] = *p++;
            buffer[n] = '\0';
            dpi = (float)atof(buffer);
            break;
        }
        free(reply);
    }

    if (dpi <= 0.0f)
        return 1.0f;
    float scale = dpi / 96.0f;
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 8.0f) scale = 8.0f;
    return scale;
}

/* ------------------------------------------------------------------ */
/* Size hints                                                         */
/* ------------------------------------------------------------------ */

static void nack__xcb_apply_size_hints(nack_window *w)
{
    nack_xcb_window *xw = nack__xcb_win(w);
    /* WM_NORMAL_HINTS is 18 CARD32s in the order fixed by ICCCM 4.1.2.3. */
    uint32_t hints[18];
    memset(hints, 0, sizeof hints);

    uint32_t flags = NACK_HINT_P_WIN_GRAVITY;
    hints[17] = XCB_GRAVITY_STATIC;

    if (!w->resizable) {
        flags |= NACK_HINT_P_MIN_SIZE | NACK_HINT_P_MAX_SIZE;
        hints[5] = hints[7] = (uint32_t)w->width;
        hints[6] = hints[8] = (uint32_t)w->height;
    } else {
        if (w->min_width > 0 || w->min_height > 0) {
            flags |= NACK_HINT_P_MIN_SIZE;
            hints[5] = (uint32_t)(w->min_width > 0 ? w->min_width : 1);
            hints[6] = (uint32_t)(w->min_height > 0 ? w->min_height : 1);
        }
        if (w->max_width > 0 || w->max_height > 0) {
            flags |= NACK_HINT_P_MAX_SIZE;
            hints[7] = (uint32_t)(w->max_width > 0 ? w->max_width : INT32_MAX);
            hints[8] = (uint32_t)(w->max_height > 0 ? w->max_height : INT32_MAX);
        }
        if (w->inc_width > 0 || w->inc_height > 0) {
            flags |= NACK_HINT_P_RESIZE_INC | NACK_HINT_P_BASE_SIZE;
            hints[9]  = (uint32_t)(w->inc_width > 0 ? w->inc_width : 1);
            hints[10] = (uint32_t)(w->inc_height > 0 ? w->inc_height : 1);
            /* Base size anchors the increment grid, so a terminal's reported
             * size is base + N*cell rather than an arbitrary pixel count. */
            hints[15] = (uint32_t)(w->min_width > 0 ? w->min_width : 0);
            hints[16] = (uint32_t)(w->min_height > 0 ? w->min_height : 0);
        }
    }

    hints[0] = flags;
    xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE, xw->handle,
                        nack__xcb.atom.WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS,
                        32, 18, hints);
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_send_wm_state(nack_window *w, uint32_t action,
                                    xcb_atom_t first, xcb_atom_t second)
{
    xcb_client_message_event_t event;
    memset(&event, 0, sizeof event);
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = nack__xcb_win(w)->handle;
    event.type = nack__xcb.atom.NET_WM_STATE;
    event.data.data32[0] = action;
    event.data.data32[1] = first;
    event.data.data32[2] = second;
    event.data.data32[3] = 1;   /* normal application source indication */
    event.data.data32[4] = 0;

    xcb_send_event(nack__xcb.connection, 0, nack__xcb.root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                   (const char *)&event);
    xcb_flush(nack__xcb.connection);
}

/* ------------------------------------------------------------------ */
/* Cursors                                                            */
/* ------------------------------------------------------------------ */

static const char *const nack__xcb_cursor_names[NACK_CURSOR_SHAPE_COUNT][3] = {
    { "default", "left_ptr", NULL },
    { "text", "xterm", NULL },
    { "crosshair", "cross", NULL },
    { "pointer", "hand2", NULL },
    { "ew-resize", "sb_h_double_arrow", NULL },
    { "ns-resize", "sb_v_double_arrow", NULL },
    { "nwse-resize", "size_fdiag", NULL },
    { "nesw-resize", "size_bdiag", NULL },
    { "all-scroll", "fleur", NULL },
    { "not-allowed", "crossed_circle", NULL },
    { "wait", "watch", NULL },
};

static xcb_cursor_t nack__xcb_get_cursor(nack_cursor_shape shape)
{
    if (nack__xcb.cursors_loaded[shape])
        return nack__xcb.cursors[shape];
    xcb_cursor_t cursor = XCB_CURSOR_NONE;
    if (nack__xcb.cursor_context) {
        for (int i = 0; i < 3 && nack__xcb_cursor_names[shape][i]; ++i) {
            cursor = xcb_cursor_load_cursor(nack__xcb.cursor_context,
                                            nack__xcb_cursor_names[shape][i]);
            if (cursor != XCB_CURSOR_NONE)
                break;
        }
    }
    nack__xcb.cursors[shape] = cursor;
    nack__xcb.cursors_loaded[shape] = true;
    return cursor;
}

static xcb_cursor_t nack__xcb_blank_cursor(void)
{
    if (nack__xcb.blank_cursor != XCB_CURSOR_NONE)
        return nack__xcb.blank_cursor;

    xcb_pixmap_t pixmap = xcb_generate_id(nack__xcb.connection);
    xcb_create_pixmap(nack__xcb.connection, 1, pixmap, nack__xcb.root, 1, 1);

    xcb_gcontext_t gc = xcb_generate_id(nack__xcb.connection);
    uint32_t value = 0;
    xcb_create_gc(nack__xcb.connection, gc, pixmap, XCB_GC_FOREGROUND, &value);
    xcb_rectangle_t rect = { 0, 0, 1, 1 };
    xcb_poly_fill_rectangle(nack__xcb.connection, pixmap, gc, 1, &rect);
    xcb_free_gc(nack__xcb.connection, gc);

    xcb_cursor_t cursor = xcb_generate_id(nack__xcb.connection);
    xcb_create_cursor(nack__xcb.connection, cursor, pixmap, pixmap,
                      0, 0, 0, 0, 0, 0, 0, 0);
    xcb_free_pixmap(nack__xcb.connection, pixmap);

    nack__xcb.blank_cursor = cursor;
    return cursor;
}

static void nack__xcb_update_cursor(nack_window *w)
{
    nack_xcb_window *xw = nack__xcb_win(w);
    xcb_cursor_t cursor = (w->cursor_mode == NACK_CURSOR_MODE_NORMAL)
                              ? nack__xcb_get_cursor(w->cursor_shape)
                              : nack__xcb_blank_cursor();
    uint32_t value = cursor;
    xcb_change_window_attributes(nack__xcb.connection, xw->handle, XCB_CW_CURSOR, &value);
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_set_cursor_shape(nack_window *w, nack_cursor_shape shape)
{
    (void)shape;
    nack__xcb_update_cursor(w);
}

static void nack__xcb_center_pointer(nack_window *w)
{
    nack_xcb_window *xw = nack__xcb_win(w);
    xw->warp_pending = true;
    xcb_warp_pointer(nack__xcb.connection, XCB_NONE, xw->handle, 0, 0, 0, 0,
                     (int16_t)(w->width / 2), (int16_t)(w->height / 2));
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_set_cursor_mode(nack_window *w, nack_cursor_mode mode)
{
    nack_xcb_window *xw = nack__xcb_win(w);
    if (mode == NACK_CURSOR_MODE_CAPTURED) {
        xw->virtual_x = w->mouse_x;
        xw->virtual_y = w->mouse_y;
        xcb_grab_pointer_cookie_t cookie = xcb_grab_pointer(
            nack__xcb.connection, 1, xw->handle,
            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                XCB_EVENT_MASK_POINTER_MOTION,
            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, xw->handle,
            nack__xcb_blank_cursor(), XCB_CURRENT_TIME);
        free(xcb_grab_pointer_reply(nack__xcb.connection, cookie, NULL));
        nack__xcb_center_pointer(w);
    } else {
        xcb_ungrab_pointer(nack__xcb.connection, XCB_CURRENT_TIME);
    }
    nack__xcb_update_cursor(w);
}

/* ------------------------------------------------------------------ */
/* Keyboard events                                                    */
/* ------------------------------------------------------------------ */

static void nack__xcb_handle_key(nack_window *w, xcb_keycode_t keycode, bool down)
{
    nack_key key = nack__xcb.keycodes[keycode];
    uint32_t mods = nack__xcb_mods_from_state();

    /* With detectable auto-repeat a held key repeats as consecutive presses. */
    bool repeat = down && key > 0 && key < NACK_KEY_COUNT && nack__g.keys[key];

    nack__emit_key(w, key, keycode, mods, down, repeat);

    if (!down || !nack__xcb.xkb_state)
        return;

    xkb_keysym_t sym = xkb_state_key_get_one_sym(nack__xcb.xkb_state, keycode);

    /* Feed the compose machine first so dead-key sequences resolve. */
    if (nack__xcb.compose_state) {
        if (xkb_compose_state_feed(nack__xcb.compose_state, sym) ==
            XKB_COMPOSE_FEED_ACCEPTED) {
            switch (xkb_compose_state_get_status(nack__xcb.compose_state)) {
            case XKB_COMPOSE_COMPOSING:
                return;    /* mid-sequence: produce no text yet */
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(nack__xcb.compose_state);
                return;
            case XKB_COMPOSE_COMPOSED: {
                char buffer[32];
                int n = xkb_compose_state_get_utf8(nack__xcb.compose_state, buffer,
                                                   sizeof buffer);
                xkb_compose_state_reset(nack__xcb.compose_state);
                if (n > 0 && n < (int)sizeof buffer)
                    nack__emit_text(w, buffer);
                return;
            }
            case XKB_COMPOSE_NOTHING:
            default:
                break;
            }
        }
    }

    /* Ctrl/Super chords are commands, not text; a terminal turns those into
     * control bytes itself from the key event. */
    if (mods & (NACK_MOD_CTRL | NACK_MOD_SUPER))
        return;

    char buffer[32];
    int n = xkb_state_key_get_utf8(nack__xcb.xkb_state, keycode, buffer, sizeof buffer);
    if (n <= 0 || n >= (int)sizeof buffer)
        return;

    uint32_t codepoint = xkb_state_key_get_utf32(nack__xcb.xkb_state, keycode);
    if (!nack__codepoint_is_text(codepoint))
        return;

    nack__emit_text(w, buffer);
}

static void nack__xcb_handle_xkb_event(xcb_generic_event_t *generic)
{
    /* All XKB events share a leading xkbType byte after the standard header. */
    typedef struct {
        uint8_t response_type;
        uint8_t xkb_type;
        uint16_t sequence;
        xcb_timestamp_t time;
        uint8_t device_id;
    } nack_xkb_any_event;

    const nack_xkb_any_event *any = (const nack_xkb_any_event *)generic;
    if (any->device_id != (uint8_t)nack__xcb.xkb_device_id)
        return;

    switch (any->xkb_type) {
    case XCB_XKB_NEW_KEYBOARD_NOTIFY:
    case XCB_XKB_MAP_NOTIFY:
        nack__xcb_load_keymap();
        break;
    case XCB_XKB_STATE_NOTIFY: {
        const xcb_xkb_state_notify_event_t *state =
            (const xcb_xkb_state_notify_event_t *)generic;
        if (nack__xcb.xkb_state)
            xkb_state_update_mask(nack__xcb.xkb_state,
                                  state->baseMods, state->latchedMods,
                                  state->lockedMods, state->baseGroup,
                                  state->latchedGroup, state->lockedGroup);
        nack__g.mods = nack__xcb_mods_from_state();
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Event dispatch                                                     */
/* ------------------------------------------------------------------ */

void nack__xcb_dispatch(xcb_generic_event_t *generic)
{
    uint8_t type = generic->response_type & 0x7F;

    if (nack__xcb.xkb_available && type == nack__xcb.xkb_event_base) {
        nack__xcb_handle_xkb_event(generic);
        return;
    }

    switch (type) {
    case XCB_SELECTION_REQUEST:
        nack__xcb_handle_selection_request((xcb_selection_request_event_t *)generic);
        return;
    case XCB_SELECTION_CLEAR:
        nack__xcb_handle_selection_clear((xcb_selection_clear_event_t *)generic);
        return;
    default:
        break;
    }

    switch (type) {
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t *event = (xcb_client_message_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->window);
        if (!w)
            break;
        if (event->type == nack__xcb.atom.WM_PROTOCOLS) {
            xcb_atom_t protocol = (xcb_atom_t)event->data.data32[0];
            if (protocol == nack__xcb.atom.WM_DELETE_WINDOW) {
                w->should_close = true;
                nack__emit_simple(w, NACK_EVENT_WINDOW_CLOSE);
            } else if (protocol == nack__xcb.atom.NET_WM_PING) {
                /* Answer the WM's liveness probe, or it marks us as hung. */
                xcb_client_message_event_t reply = *event;
                reply.window = nack__xcb.root;
                xcb_send_event(nack__xcb.connection, 0, nack__xcb.root,
                               XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                               (const char *)&reply);
                xcb_flush(nack__xcb.connection);
            }
        }
        break;
    }

    case XCB_CONFIGURE_NOTIFY: {
        xcb_configure_notify_event_t *event = (xcb_configure_notify_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->window);
        if (!w)
            break;
        nack__emit_resize(w, event->width, event->height, event->width, event->height);
        if (event->x != w->pos_x || event->y != w->pos_y) {
            w->pos_x = event->x;
            w->pos_y = event->y;
            nack_event *ev = nack__event_begin(NACK_EVENT_WINDOW_MOVE, w);
            ev->move.x = event->x;
            ev->move.y = event->y;
            nack__push_event(ev);
        }
        break;
    }

    case XCB_EXPOSE: {
        xcb_expose_event_t *event = (xcb_expose_event_t *)generic;
        if (event->count != 0)
            break;
        nack_window *w = nack__xcb_lookup(event->window);
        if (w)
            nack__emit_simple(w, NACK_EVENT_WINDOW_EXPOSE);
        break;
    }

    case XCB_FOCUS_IN: {
        xcb_focus_in_event_t *event = (xcb_focus_in_event_t *)generic;
        if (event->mode == XCB_NOTIFY_MODE_GRAB ||
            event->mode == XCB_NOTIFY_MODE_UNGRAB)
            break;
        nack_window *w = nack__xcb_lookup(event->event);
        if (!w)
            break;
        if (w->cursor_mode == NACK_CURSOR_MODE_CAPTURED)
            nack__xcb_set_cursor_mode(w, NACK_CURSOR_MODE_CAPTURED);
        nack__emit_focus(w, true);
        break;
    }

    case XCB_FOCUS_OUT: {
        xcb_focus_out_event_t *event = (xcb_focus_out_event_t *)generic;
        if (event->mode == XCB_NOTIFY_MODE_GRAB ||
            event->mode == XCB_NOTIFY_MODE_UNGRAB)
            break;
        nack_window *w = nack__xcb_lookup(event->event);
        if (!w)
            break;
        if (w->cursor_mode == NACK_CURSOR_MODE_CAPTURED)
            xcb_ungrab_pointer(nack__xcb.connection, XCB_CURRENT_TIME);
        if (nack__xcb.compose_state)
            xkb_compose_state_reset(nack__xcb.compose_state);
        nack__emit_focus(w, false);
        break;
    }

    case XCB_ENTER_NOTIFY: {
        xcb_enter_notify_event_t *event = (xcb_enter_notify_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->event);
        if (!w)
            break;
        w->mouse_x = event->event_x;
        w->mouse_y = event->event_y;
        nack__emit_simple(w, NACK_EVENT_MOUSE_ENTER);
        break;
    }

    case XCB_LEAVE_NOTIFY: {
        xcb_leave_notify_event_t *event = (xcb_leave_notify_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->event);
        if (w)
            nack__emit_simple(w, NACK_EVENT_MOUSE_LEAVE);
        break;
    }

    case XCB_MOTION_NOTIFY: {
        xcb_motion_notify_event_t *event = (xcb_motion_notify_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->event);
        if (!w)
            break;
        nack_xcb_window *xw = nack__xcb_win(w);
        int x = event->event_x, y = event->event_y;

        if (w->cursor_mode == NACK_CURSOR_MODE_CAPTURED) {
            if (xw->warp_pending && x == w->width / 2 && y == w->height / 2) {
                xw->warp_pending = false;
                w->mouse_x = x;
                w->mouse_y = y;
                break;
            }
            double dx = x - w->mouse_x;
            double dy = y - w->mouse_y;
            xw->virtual_x += dx;
            xw->virtual_y += dy;
            w->mouse_x = x;
            w->mouse_y = y;

            nack_event *ev = nack__event_begin(NACK_EVENT_MOUSE_MOVE, w);
            ev->motion.x = xw->virtual_x;
            ev->motion.y = xw->virtual_y;
            ev->motion.dx = dx;
            ev->motion.dy = dy;
            ev->motion.mods = nack__xcb_mods_from_state();
            nack__push_event(ev);

            if (x < w->width / 4 || x > (w->width * 3) / 4 ||
                y < w->height / 4 || y > (w->height * 3) / 4)
                nack__xcb_center_pointer(w);
        } else {
            nack__emit_mouse_move(w, x, y, nack__xcb_mods_from_state());
        }
        break;
    }

    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE: {
        xcb_button_press_event_t *event = (xcb_button_press_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->event);
        if (!w)
            break;
        bool down = (type == XCB_BUTTON_PRESS);
        uint32_t mods = nack__xcb_mods_from_state();

        /* Buttons 4-7 are the classic wheel encoding. */
        if (event->detail >= 4 && event->detail <= 7) {
            if (!down)
                break;
            double dx = 0.0, dy = 0.0;
            switch (event->detail) {
            case 4: dy =  1.0; break;
            case 5: dy = -1.0; break;
            case 6: dx =  1.0; break;
            case 7: dx = -1.0; break;
            default: break;
            }
            nack__emit_scroll(w, dx, dy, mods, false);
            break;
        }

        int button;
        switch (event->detail) {
        case 1: button = NACK_MOUSE_LEFT; break;
        case 2: button = NACK_MOUSE_MIDDLE; break;
        case 3: button = NACK_MOUSE_RIGHT; break;
        case 8: button = NACK_MOUSE_X1; break;
        case 9: button = NACK_MOUSE_X2; break;
        default: button = event->detail - 1; break;
        }
        nack__emit_mouse_button(w, button, down, event->event_x, event->event_y, mods);
        break;
    }

    case XCB_KEY_PRESS: {
        xcb_key_press_event_t *event = (xcb_key_press_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->event);
        if (w)
            nack__xcb_handle_key(w, event->detail, true);
        break;
    }

    case XCB_KEY_RELEASE: {
        xcb_key_release_event_t *event = (xcb_key_release_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->event);
        if (w)
            nack__xcb_handle_key(w, event->detail, false);
        break;
    }

    case XCB_PROPERTY_NOTIFY: {
        xcb_property_notify_event_t *event = (xcb_property_notify_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->window);
        if (!w || event->atom != nack__xcb.atom.NET_WM_STATE)
            break;

        xcb_get_property_cookie_t cookie =
            xcb_get_property(nack__xcb.connection, 0, event->window,
                             nack__xcb.atom.NET_WM_STATE, XCB_ATOM_ATOM, 0, 128);
        xcb_get_property_reply_t *reply =
            xcb_get_property_reply(nack__xcb.connection, cookie, NULL);
        if (!reply)
            break;

        bool fullscreen = false, max_v = false, max_h = false, hidden = false;
        xcb_atom_t *atoms = (xcb_atom_t *)xcb_get_property_value(reply);
        int count = xcb_get_property_value_length(reply) / (int)sizeof(xcb_atom_t);
        for (int i = 0; i < count; ++i) {
            if (atoms[i] == nack__xcb.atom.NET_WM_STATE_FULLSCREEN) fullscreen = true;
            else if (atoms[i] == nack__xcb.atom.NET_WM_STATE_MAXIMIZED_VERT) max_v = true;
            else if (atoms[i] == nack__xcb.atom.NET_WM_STATE_MAXIMIZED_HORZ) max_h = true;
            else if (atoms[i] == nack__xcb.atom.NET_WM_STATE_HIDDEN) hidden = true;
        }
        free(reply);

        w->fullscreen = fullscreen;
        bool maximized = max_v && max_h;
        if (maximized != w->maximized) {
            w->maximized = maximized;
            nack__emit_simple(w, maximized ? NACK_EVENT_WINDOW_MAXIMIZE
                                           : NACK_EVENT_WINDOW_RESTORE);
        }
        if (hidden != w->minimized) {
            w->minimized = hidden;
            nack__emit_simple(w, hidden ? NACK_EVENT_WINDOW_MINIMIZE
                                        : NACK_EVENT_WINDOW_RESTORE);
        }
        break;
    }

    case XCB_MAP_NOTIFY: {
        xcb_map_notify_event_t *event = (xcb_map_notify_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->window);
        if (w)
            w->visible = true;
        break;
    }

    case XCB_UNMAP_NOTIFY: {
        xcb_unmap_notify_event_t *event = (xcb_unmap_notify_event_t *)generic;
        nack_window *w = nack__xcb_lookup(event->window);
        if (w)
            w->visible = false;
        break;
    }

    default:
        break;
    }
}

static void nack__xcb_drain(void)
{
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(nack__xcb.connection)) != NULL) {
        if (event->response_type != 0)     /* 0 = error reply, ignore */
            nack__xcb_dispatch(event);
        free(event);
    }
}

static void nack__xcb_pump_events(double timeout)
{
    nack__xcb_drain();
    if (!nack__queue_empty() || timeout == 0.0)
        return;

    xcb_flush(nack__xcb.connection);

    struct pollfd fds[2];
    fds[0].fd = xcb_get_file_descriptor(nack__xcb.connection);
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = nack__xcb.wakeup_pipe[0];
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    int ms = -1;
    if (timeout > 0.0) {
        double clamped = timeout * 1000.0;
        if (clamped > (double)INT_MAX)
            clamped = (double)INT_MAX;
        ms = (int)clamped;
    }

    int rc = poll(fds, 2, ms);
    if (rc < 0 && errno != EINTR)
        return;

    if (fds[1].revents & POLLIN) {
        char scratch[64];
        while (read(nack__xcb.wakeup_pipe[0], scratch, sizeof scratch) > 0)
            ;
        nack__emit_simple(NULL, NACK_EVENT_WAKEUP);
    }

    nack__xcb_drain();
}

static void nack__xcb_wakeup(void)
{
    const char byte = 1;
    ssize_t rc;
    do {
        rc = write(nack__xcb.wakeup_pipe[1], &byte, 1);
    } while (rc < 0 && errno == EINTR);
    (void)rc;
}

/* ------------------------------------------------------------------ */
/* Window management                                                  */
/* ------------------------------------------------------------------ */

static void nack__xcb_set_title(nack_window *w, const char *title)
{
    nack_xcb_window *xw = nack__xcb_win(w);
    uint32_t len = (uint32_t)strlen(title);
    xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE, xw->handle,
                        nack__xcb.atom.NET_WM_NAME, nack__xcb.atom.UTF8_STRING,
                        8, len, title);
    xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE, xw->handle,
                        nack__xcb.atom.NET_WM_ICON_NAME, nack__xcb.atom.UTF8_STRING,
                        8, len, title);
    /* Legacy WM_NAME for window managers that predate _NET_WM_NAME. */
    xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE, xw->handle,
                        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, len, title);
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_set_decorated(nack_window *w, bool decorated)
{
    if (decorated)
        return;
    /* Motif hints remain the only widely honoured way to drop decorations. */
    uint32_t hints[5] = { 2 /* MWM_HINTS_DECORATIONS */, 0, 0, 0, 0 };
    xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE,
                        nack__xcb_win(w)->handle, nack__xcb.atom.MOTIF_WM_HINTS,
                        nack__xcb.atom.MOTIF_WM_HINTS, 32, 5, hints);
}

/* Finds the depth/visual pairing that matches the EGL config's visual id. */
static bool nack__xcb_find_visual(xcb_visualid_t visual_id, uint8_t *out_depth)
{
    xcb_depth_iterator_t depth = xcb_screen_allowed_depths_iterator(nack__xcb.screen);
    for (; depth.rem; xcb_depth_next(&depth)) {
        xcb_visualtype_iterator_t visual = xcb_depth_visuals_iterator(depth.data);
        for (; visual.rem; xcb_visualtype_next(&visual)) {
            if (visual.data->visual_id == visual_id) {
                *out_depth = depth.data->depth;
                return true;
            }
        }
    }
    return false;
}

static bool nack__xcb_window_create(nack_window *w, const nack_window_desc *desc)
{
    nack_xcb_window *xw = (nack_xcb_window *)nack__calloc(1, sizeof *xw);
    if (!xw)
        return false;
    xw->surface = EGL_NO_SURFACE;
    w->native = xw;

    xcb_visualid_t visual_id = nack__xcb.screen->root_visual;
    uint8_t depth = XCB_COPY_FROM_PARENT;

    /* The window's visual has to match the EGL config that will back it, so
     * the config is chosen here rather than at context creation time. */
    if (nack__egl.initialized) {
        EGLConfig config;
        EGLint egl_visual = 0;
        if (nack__egl_choose_config(&w->framebuffer, NACK_GL_PROFILE_CORE, 3,
                                    &config, &egl_visual)) {
            xw->config = config;
            xw->has_config = true;
            if (egl_visual != 0) {
                uint8_t found_depth = 0;
                if (nack__xcb_find_visual((xcb_visualid_t)egl_visual, &found_depth)) {
                    visual_id = (xcb_visualid_t)egl_visual;
                    depth = found_depth;
                }
            }
        }
    }

    xw->colormap = xcb_generate_id(nack__xcb.connection);
    xcb_create_colormap(nack__xcb.connection, XCB_COLORMAP_ALLOC_NONE, xw->colormap,
                        nack__xcb.root, visual_id);

    const uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL |
                          XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
    const uint32_t values[4] = {
        0,   /* background: black, so an unpainted frame is not garbage */
        0,   /* border pixel is mandatory when the depth differs from parent */
        XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE |
            XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_KEY_PRESS |
            XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
            XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
            XCB_EVENT_MASK_PROPERTY_CHANGE,
        xw->colormap
    };

    xw->handle = xcb_generate_id(nack__xcb.connection);
    xcb_void_cookie_t cookie = xcb_create_window_checked(
        nack__xcb.connection, depth, xw->handle, nack__xcb.root,
        0, 0, (uint16_t)w->width, (uint16_t)w->height, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, visual_id, mask, values);

    xcb_generic_error_t *error = xcb_request_check(nack__xcb.connection, cookie);
    if (error) {
        uint8_t code = error->error_code;
        free(error);
        free(xw);
        w->native = NULL;
        return nack__fail(NACK_ERROR_PLATFORM, "xcb_create_window failed (code %u)",
                          (unsigned)code);
    }

    xw->egl_native = xw->handle;

    xcb_atom_t protocols[2] = { nack__xcb.atom.WM_DELETE_WINDOW,
                                nack__xcb.atom.NET_WM_PING };
    xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE, xw->handle,
                        nack__xcb.atom.WM_PROTOCOLS, XCB_ATOM_ATOM, 32, 2, protocols);

    uint32_t pid = (uint32_t)getpid();
    xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE, xw->handle,
                        nack__xcb.atom.NET_WM_PID, XCB_ATOM_CARDINAL, 32, 1, &pid);

    /* WM_CLASS is instance\0class\0, both taken from the app id. */
    {
        size_t len = strlen(nack__g.app_id);
        size_t total = (len + 1) * 2;
        char *wm_class = (char *)malloc(total);
        if (wm_class) {
            memcpy(wm_class, nack__g.app_id, len + 1);
            memcpy(wm_class + len + 1, nack__g.app_id, len + 1);
            xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE, xw->handle,
                                XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
                                (uint32_t)total, wm_class);
            free(wm_class);
        }
    }

    nack__xcb_set_title(w, w->title);
    nack__xcb_set_decorated(w, w->decorated);
    nack__xcb_apply_size_hints(w);

    w->scale = nack__xcb.scale;
    w->fb_width = w->width;
    w->fb_height = w->height;

    xcb_flush(nack__xcb.connection);
    return true;
}

static void nack__xcb_window_destroy(nack_window *w)
{
    nack_xcb_window *xw = nack__xcb_win(w);
    if (!xw)
        return;
    if (xw->surface != EGL_NO_SURFACE)
        eglDestroySurface(nack__egl.display, xw->surface);
    if (xw->handle) {
        xcb_destroy_window(nack__xcb.connection, xw->handle);
        xw->handle = XCB_WINDOW_NONE;
    }
    if (xw->colormap)
        xcb_free_colormap(nack__xcb.connection, xw->colormap);
    xcb_flush(nack__xcb.connection);
    free(xw);
    w->native = NULL;
}

static void nack__xcb_window_show(nack_window *w, bool show)
{
    nack_xcb_window *xw = nack__xcb_win(w);
    if (show)
        xcb_map_window(nack__xcb.connection, xw->handle);
    else
        xcb_unmap_window(nack__xcb.connection, xw->handle);
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_window_focus(nack_window *w)
{
    nack_xcb_window *xw = nack__xcb_win(w);
    if (nack__xcb_wm_supports(nack__xcb.atom.NET_ACTIVE_WINDOW)) {
        xcb_client_message_event_t event;
        memset(&event, 0, sizeof event);
        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.window = xw->handle;
        event.type = nack__xcb.atom.NET_ACTIVE_WINDOW;
        event.data.data32[0] = 1;
        event.data.data32[1] = XCB_CURRENT_TIME;
        xcb_send_event(nack__xcb.connection, 0, nack__xcb.root,
                       XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                           XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                       (const char *)&event);
    } else {
        uint32_t values[] = { XCB_STACK_MODE_ABOVE };
        xcb_configure_window(nack__xcb.connection, xw->handle,
                             XCB_CONFIG_WINDOW_STACK_MODE, values);
        xcb_set_input_focus(nack__xcb.connection, XCB_INPUT_FOCUS_PARENT,
                            xw->handle, XCB_CURRENT_TIME);
    }
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_window_set_size(nack_window *w, int width, int height)
{
    if (!w->resizable) {
        w->width = width;
        w->height = height;
        nack__xcb_apply_size_hints(w);
    }
    uint32_t values[2] = { (uint32_t)width, (uint32_t)height };
    xcb_configure_window(nack__xcb.connection, nack__xcb_win(w)->handle,
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_window_set_position(nack_window *w, int x, int y)
{
    uint32_t values[2] = { (uint32_t)x, (uint32_t)y };
    xcb_configure_window(nack__xcb.connection, nack__xcb_win(w)->handle,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_window_set_fullscreen(nack_window *w, bool fullscreen)
{
    if (!nack__xcb_wm_supports(nack__xcb.atom.NET_WM_STATE_FULLSCREEN)) {
        nack__fail(NACK_ERROR_UNSUPPORTED, "window manager has no fullscreen support");
        return;
    }
    nack_xcb_window *xw = nack__xcb_win(w);
    if (fullscreen && !w->fullscreen) {
        xw->restore_x = w->pos_x;
        xw->restore_y = w->pos_y;
        xw->restore_w = w->width;
        xw->restore_h = w->height;
    }
    nack__xcb_send_wm_state(w, fullscreen ? NACK_NET_WM_STATE_ADD
                                          : NACK_NET_WM_STATE_REMOVE,
                            nack__xcb.atom.NET_WM_STATE_FULLSCREEN, XCB_ATOM_NONE);
    w->fullscreen = fullscreen;
}

static void nack__xcb_window_minimize(nack_window *w)
{
    /* ICCCM: iconify by sending WM_CHANGE_STATE with IconicState. */
    xcb_client_message_event_t event;
    memset(&event, 0, sizeof event);
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = nack__xcb_win(w)->handle;
    event.type = nack__xcb.atom.WM_CHANGE_STATE;
    event.data.data32[0] = 3;   /* IconicState */
    xcb_send_event(nack__xcb.connection, 0, nack__xcb.root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                   (const char *)&event);
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_window_maximize(nack_window *w)
{
    nack__xcb_send_wm_state(w, NACK_NET_WM_STATE_ADD,
                            nack__xcb.atom.NET_WM_STATE_MAXIMIZED_VERT,
                            nack__xcb.atom.NET_WM_STATE_MAXIMIZED_HORZ);
}

static void nack__xcb_window_restore(nack_window *w)
{
    if (w->fullscreen)
        nack__xcb_window_set_fullscreen(w, false);
    if (w->maximized)
        nack__xcb_send_wm_state(w, NACK_NET_WM_STATE_REMOVE,
                                nack__xcb.atom.NET_WM_STATE_MAXIMIZED_VERT,
                                nack__xcb.atom.NET_WM_STATE_MAXIMIZED_HORZ);
    if (w->minimized)
        xcb_map_window(nack__xcb.connection, nack__xcb_win(w)->handle);
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_window_request_attention(nack_window *w)
{
    nack__xcb_send_wm_state(w, NACK_NET_WM_STATE_ADD,
                            nack__xcb.atom.NET_WM_STATE_DEMANDS_ATTENTION,
                            XCB_ATOM_NONE);
}

static void nack__xcb_window_request_redraw(nack_window *w)
{
    xcb_expose_event_t event;
    memset(&event, 0, sizeof event);
    event.response_type = XCB_EXPOSE;
    event.window = nack__xcb_win(w)->handle;
    event.width = (uint16_t)w->width;
    event.height = (uint16_t)w->height;
    event.count = 0;
    xcb_send_event(nack__xcb.connection, 0, nack__xcb_win(w)->handle,
                   XCB_EVENT_MASK_EXPOSURE, (const char *)&event);
    xcb_flush(nack__xcb.connection);
}

static void nack__xcb_window_get_native(const nack_window *w, nack_native_window *out)
{
    nack_xcb_window *xw = (nack_xcb_window *)w->native;
    out->display = nack__xcb.connection;
    out->surface = nack__xcb.xlib_display;   /* NULL on the pure-XCB path */
    out->handle = xw ? (uintptr_t)xw->handle : 0;
}

/* ------------------------------------------------------------------ */
/* OpenGL                                                             */
/* ------------------------------------------------------------------ */

static const nack_backend_vt *nack__xcb_vt_ptr(void);

static bool nack__xcb_ensure_surface(nack_window *w, EGLConfig config)
{
    nack_xcb_window *xw = nack__xcb_win(w);
    if (xw->surface != EGL_NO_SURFACE)
        return true;

    /* With EGL_EXT_platform_xcb the native window is a *pointer* to an
     * xcb_window_t; the legacy entry point takes the id by value. */
    bool use_pointer = (nack__xcb.xlib_display == NULL) &&
                       nack__egl.create_platform_window_surface != NULL;
    void *native = use_pointer ? (void *)&xw->egl_native
                               : (void *)(uintptr_t)xw->handle;

    xw->surface = nack__egl_create_window_surface(config, native, use_pointer,
                                                  w->framebuffer.srgb);
    return xw->surface != EGL_NO_SURFACE;
}

static nack_gl_context *nack__xcb_gl_create(nack_window *w, const nack_gl_desc *desc)
{
    if (!nack__egl.initialized) {
        nack__fail(NACK_ERROR_UNSUPPORTED, "EGL is not available");
        return NULL;
    }
    nack_xcb_window *xw = nack__xcb_win(w);
    if (!xw->has_config) {
        nack__fail(NACK_ERROR_NO_PIXEL_FORMAT,
                   "window was created without a usable EGL config");
        return NULL;
    }
    if (!nack__xcb_ensure_surface(w, xw->config))
        return NULL;
    return nack__egl_create_context(w, desc, xw->config, nack__xcb_vt_ptr());
}

static void nack__xcb_gl_destroy(nack_gl_context *ctx)
{
    nack__egl_destroy_context(ctx);
}

static bool nack__xcb_gl_make_current(nack_window *w, nack_gl_context *ctx)
{
    if (!ctx)
        return nack__egl_make_current(EGL_NO_SURFACE, NULL);
    if (!w)
        return nack__fail(NACK_ERROR_INVALID_ARGUMENT,
                          "nack_gl_make_current needs a window for this context");
    nack_xcb_window *xw = nack__xcb_win(w);
    if (xw->surface == EGL_NO_SURFACE &&
        !nack__xcb_ensure_surface(w, ((nack_egl_context *)ctx->native)->config))
        return false;
    return nack__egl_make_current(xw->surface, ctx);
}

static void nack__xcb_gl_swap_buffers(nack_window *w)
{
    nack__egl_swap_buffers(nack__xcb_win(w)->surface);
}

/* ------------------------------------------------------------------ */
/* Init / shutdown                                                    */
/* ------------------------------------------------------------------ */

static bool nack__xcb_egl_client_has(const char *name)
{
    const char *exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!exts)
        return false;
    size_t len = strlen(name);
    const char *p = exts;
    while ((p = strstr(p, name)) != NULL) {
        char after = p[len];
        if ((p == exts || p[-1] == ' ') && (after == ' ' || after == '\0'))
            return true;
        p += len;
    }
    return false;
}

static bool nack__xcb_connect(void)
{
    /*
     * EGL_EXT_platform_xcb lets us stay on a pure XCB connection. Without it
     * (notably on the NVIDIA driver) EGL needs an Xlib Display*, so open one
     * and borrow its XCB connection rather than running two connections.
     */
    bool want_xcb_platform = nack__xcb_egl_client_has("EGL_EXT_platform_xcb");

#if defined(NACK_XCB_XLIB_FALLBACK)
    if (!want_xcb_platform) {
        Display *display = XOpenDisplay(NULL);
        if (display) {
            nack__xcb.xlib_display = display;
            nack__xcb.connection = XGetXCBConnection(display);
            if (nack__xcb.connection) {
                /* Xlib must not consume events we want to read through XCB. */
                XSetEventQueueOwner(display, XCBOwnsEventQueue);
                nack__xcb.screen_number = DefaultScreen(display);
                return true;
            }
            XCloseDisplay(display);
            nack__xcb.xlib_display = NULL;
        }
    }
#else
    (void)want_xcb_platform;
#endif

    nack__xcb.connection = xcb_connect(NULL, &nack__xcb.screen_number);
    if (!nack__xcb.connection || xcb_connection_has_error(nack__xcb.connection)) {
        if (nack__xcb.connection) {
            xcb_disconnect(nack__xcb.connection);
            nack__xcb.connection = NULL;
        }
        return false;
    }
    return true;
}

static bool nack__xcb_init(const nack_init_desc *desc)
{
    (void)desc;
    memset(&nack__xcb, 0, sizeof nack__xcb);
    nack__xcb.wakeup_pipe[0] = nack__xcb.wakeup_pipe[1] = -1;

    setlocale(LC_CTYPE, "");

    if (!nack__xcb_connect())
        return nack__fail(NACK_ERROR_NO_BACKEND, "cannot connect to X display '%s'",
                          getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");

    /* Walk the setup to the screen this connection defaulted to. */
    const xcb_setup_t *setup = xcb_get_setup(nack__xcb.connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < nack__xcb.screen_number && iter.rem; ++i)
        xcb_screen_next(&iter);
    if (!iter.rem) {
        xcb_disconnect(nack__xcb.connection);
        nack__xcb.connection = NULL;
        return nack__fail(NACK_ERROR_NO_BACKEND, "X screen %d does not exist",
                          nack__xcb.screen_number);
    }
    nack__xcb.screen = iter.data;
    nack__xcb.root = nack__xcb.screen->root;

    if (pipe(nack__xcb.wakeup_pipe) != 0) {
        xcb_disconnect(nack__xcb.connection);
        nack__xcb.connection = NULL;
        return nack__fail(NACK_ERROR_PLATFORM, "pipe() failed: %s", strerror(errno));
    }
    for (int i = 0; i < 2; ++i) {
        int flags = fcntl(nack__xcb.wakeup_pipe[i], F_GETFL, 0);
        fcntl(nack__xcb.wakeup_pipe[i], F_SETFL, flags | O_NONBLOCK);
        flags = fcntl(nack__xcb.wakeup_pipe[i], F_GETFD, 0);
        fcntl(nack__xcb.wakeup_pipe[i], F_SETFD, flags | FD_CLOEXEC);
    }

    nack__xcb_intern_atoms();
    nack__xcb.scale = nack__xcb_query_scale();

    if (!nack__xcb_init_xkb())
        nack__log("nack: XKB unavailable, keyboard input will be limited");

    if (xcb_cursor_context_new(nack__xcb.connection, nack__xcb.screen,
                               &nack__xcb.cursor_context) < 0)
        nack__xcb.cursor_context = NULL;

    /* Unmapped InputOnly window that holds selection ownership, so clipboard
     * contents outlive any particular application window. */
    nack__xcb.helper = xcb_generate_id(nack__xcb.connection);
    uint32_t helper_mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(nack__xcb.connection, XCB_COPY_FROM_PARENT, nack__xcb.helper,
                      nack__xcb.root, -1, -1, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                      XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &helper_mask);

    /* EGL: pure XCB where supported, Xlib display where it is not. */
    bool egl_ok = false;
    if (!nack__xcb.xlib_display) {
        EGLAttrib attribs[] = { EGL_PLATFORM_XCB_SCREEN_EXT,
                                (EGLAttrib)nack__xcb.screen_number, EGL_NONE };
        egl_ok = nack__egl_init(EGL_PLATFORM_XCB_EXT, nack__xcb.connection, attribs);
    }
    if (!egl_ok && nack__xcb.xlib_display)
        egl_ok = nack__egl_init(EGL_PLATFORM_X11_KHR, nack__xcb.xlib_display, NULL);
    if (!egl_ok)
        nack__log("nack: EGL unavailable; windows will have no OpenGL support");

    xcb_flush(nack__xcb.connection);
    return true;
}

static void nack__xcb_shutdown(void)
{
    if (!nack__xcb.connection)
        return;

    nack__xcb_clipboard_shutdown();
    nack__egl_terminate();

    for (int i = 0; i < NACK_CURSOR_SHAPE_COUNT; ++i) {
        if (nack__xcb.cursors_loaded[i] && nack__xcb.cursors[i] != XCB_CURSOR_NONE)
            xcb_free_cursor(nack__xcb.connection, nack__xcb.cursors[i]);
    }
    if (nack__xcb.blank_cursor != XCB_CURSOR_NONE)
        xcb_free_cursor(nack__xcb.connection, nack__xcb.blank_cursor);
    if (nack__xcb.cursor_context)
        xcb_cursor_context_free(nack__xcb.cursor_context);
    if (nack__xcb.helper)
        xcb_destroy_window(nack__xcb.connection, nack__xcb.helper);

    if (nack__xcb.compose_state) xkb_compose_state_unref(nack__xcb.compose_state);
    if (nack__xcb.compose_table) xkb_compose_table_unref(nack__xcb.compose_table);
    nack__xcb_release_keymap();
    if (nack__xcb.xkb_context) xkb_context_unref(nack__xcb.xkb_context);

    if (nack__xcb.wakeup_pipe[0] >= 0) close(nack__xcb.wakeup_pipe[0]);
    if (nack__xcb.wakeup_pipe[1] >= 0) close(nack__xcb.wakeup_pipe[1]);

    xcb_flush(nack__xcb.connection);
#if defined(NACK_XCB_XLIB_FALLBACK)
    if (nack__xcb.xlib_display)
        XCloseDisplay((Display *)nack__xcb.xlib_display);
    else
        xcb_disconnect(nack__xcb.connection);
#else
    xcb_disconnect(nack__xcb.connection);
#endif

    memset(&nack__xcb, 0, sizeof nack__xcb);
}

/* ------------------------------------------------------------------ */

static const nack_backend_vt nack__xcb_vt = {
    .name = "xcb",
    .id = NACK_BACKEND_X11,
    .init = nack__xcb_init,
    .shutdown = nack__xcb_shutdown,
    .window_create = nack__xcb_window_create,
    .window_destroy = nack__xcb_window_destroy,
    .window_show = nack__xcb_window_show,
    .window_focus = nack__xcb_window_focus,
    .window_set_title = nack__xcb_set_title,
    .window_set_size = nack__xcb_window_set_size,
    .window_set_position = nack__xcb_window_set_position,
    .window_apply_size_hints = nack__xcb_apply_size_hints,
    .window_set_fullscreen = nack__xcb_window_set_fullscreen,
    .window_minimize = nack__xcb_window_minimize,
    .window_maximize = nack__xcb_window_maximize,
    .window_restore = nack__xcb_window_restore,
    .window_request_attention = nack__xcb_window_request_attention,
    .window_request_redraw = nack__xcb_window_request_redraw,
    .window_set_cursor_shape = nack__xcb_set_cursor_shape,
    .window_set_cursor_mode = nack__xcb_set_cursor_mode,
    .window_get_native = nack__xcb_window_get_native,
    .pump_events = nack__xcb_pump_events,
    .wakeup = nack__xcb_wakeup,
    .gl_create = nack__xcb_gl_create,
    .gl_destroy = nack__xcb_gl_destroy,
    .gl_make_current = nack__xcb_gl_make_current,
    .gl_swap_buffers = nack__xcb_gl_swap_buffers,
    .gl_set_swap_interval = nack__egl_set_swap_interval,
    .gl_get_proc_address = nack__egl_get_proc_address,
    .clipboard_set = nack__xcb_clipboard_set,
    .clipboard_get = nack__xcb_clipboard_get,
    .primary_set = nack__xcb_primary_set,
    .primary_get = nack__xcb_primary_get,
};

static const nack_backend_vt *nack__xcb_vt_ptr(void)
{
    return &nack__xcb_vt;
}

const nack_backend_vt *nack__backend_x11(void)
{
    return &nack__xcb_vt;
}
