/*
 * Wayland input: seat capabilities, keyboard (including the key repeat that
 * Wayland delegates to the client), pointer, and cursor themes.
 */
#include "nack_wayland.h"
#include "../common/nack_xkb_keys.h"

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

/* Wayland reports evdev key codes offset by 8, matching X11 keycodes, so the
 * xkbcommon keycode is the wire value plus 8. */
#define NACK_WL_KEYCODE(raw) ((raw) + 8u)

static uint32_t nack__wl_mods(void)
{
    if (!nack__wl.xkb_state)
        return 0;
    struct xkb_state *state = nack__wl.xkb_state;
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
/* Text production                                                    */
/* ------------------------------------------------------------------ */

static void nack__wl_emit_text_for(struct nack_window *w, xkb_keycode_t keycode, uint32_t mods)
{
    if (!nack__wl.xkb_state)
        return;

    xkb_keysym_t sym = xkb_state_key_get_one_sym(nack__wl.xkb_state, keycode);

    if (nack__wl.compose_state) {
        if (xkb_compose_state_feed(nack__wl.compose_state, sym) ==
            XKB_COMPOSE_FEED_ACCEPTED) {
            switch (xkb_compose_state_get_status(nack__wl.compose_state)) {
            case XKB_COMPOSE_COMPOSING:
                return;
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(nack__wl.compose_state);
                return;
            case XKB_COMPOSE_COMPOSED: {
                char buffer[32];
                int n = xkb_compose_state_get_utf8(nack__wl.compose_state, buffer,
                                                   sizeof buffer);
                xkb_compose_state_reset(nack__wl.compose_state);
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

    if (mods & (NACK_MOD_CTRL | NACK_MOD_SUPER))
        return;

    char buffer[32];
    int n = xkb_state_key_get_utf8(nack__wl.xkb_state, keycode, buffer, sizeof buffer);
    if (n <= 0 || n >= (int)sizeof buffer)
        return;

    uint32_t codepoint = xkb_state_key_get_utf32(nack__wl.xkb_state, keycode);
    if (!nack__codepoint_is_text(codepoint))
        return;

    nack__emit_text(w, buffer);
}

/* ------------------------------------------------------------------ */
/* Key repeat                                                         */
/* ------------------------------------------------------------------ */

static void nack__wl_start_repeat(struct nack_window *w, uint32_t raw_key)
{
    if (nack__wl.repeat_rate <= 0 || !nack__wl.xkb_keymap)
        return;
    if (!xkb_keymap_key_repeats(nack__wl.xkb_keymap, NACK_WL_KEYCODE(raw_key)))
        return;
    nack__wl.repeat_key = raw_key + 1;   /* +1 so 0 stays the idle marker */
    nack__wl.repeat_window = w;
    nack__wl.repeat_next_ns =
        nack_time_ns() + (uint64_t)nack__wl.repeat_delay * 1000000ull;
}

static void nack__wl_stop_repeat(void)
{
    nack__wl.repeat_key = 0;
    nack__wl.repeat_window = NULL;
}

double nack__wl_next_repeat_timeout(void)
{
    if (!nack__wl.repeat_key || !nack__wl.repeat_window)
        return -1.0;
    uint64_t now = nack_time_ns();
    if (now >= nack__wl.repeat_next_ns)
        return 0.0;
    return (double)(nack__wl.repeat_next_ns - now) / 1e9;
}

void nack__wl_pump_key_repeat(void)
{
    if (!nack__wl.repeat_key || !nack__wl.repeat_window)
        return;

    uint64_t now = nack_time_ns();
    uint64_t interval = nack__wl.repeat_rate > 0
                            ? 1000000000ull / (uint64_t)nack__wl.repeat_rate
                            : 0;
    if (interval == 0)
        return;

    /* Catch up without spinning if the caller was blocked for a long time. */
    int guard = 0;
    while (now >= nack__wl.repeat_next_ns && guard++ < 32) {
        uint32_t raw_key = nack__wl.repeat_key - 1;
        xkb_keycode_t keycode = NACK_WL_KEYCODE(raw_key);
        enum nack_key key = keycode < 256 ? nack__wl.keycodes[keycode] : NACK_KEY_UNKNOWN;
        uint32_t mods = nack__wl_mods();

        nack__emit_key(nack__wl.repeat_window, key, raw_key, mods, true, true);
        nack__wl_emit_text_for(nack__wl.repeat_window, keycode, mods);

        nack__wl.repeat_next_ns += interval;
    }
    if (guard >= 32)
        nack__wl.repeat_next_ns = now + interval;
}

/* ------------------------------------------------------------------ */
/* Keyboard                                                           */
/* ------------------------------------------------------------------ */

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format,
                            int32_t fd, uint32_t size)
{
    (void)data; (void)keyboard;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map = (char *)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_string(
        nack__wl.xkb_context, map, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);

    if (!keymap)
        return;

    struct xkb_state *state = xkb_state_new(keymap);
    if (!state) {
        xkb_keymap_unref(keymap);
        return;
    }

    if (nack__wl.xkb_state)  xkb_state_unref(nack__wl.xkb_state);
    if (nack__wl.xkb_keymap) xkb_keymap_unref(nack__wl.xkb_keymap);
    nack__wl.xkb_keymap = keymap;
    nack__wl.xkb_state = state;
    nack__xkb_build_keycodes(nack__wl.xkb_keymap, nack__wl.keycodes);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys)
{
    (void)data; (void)keyboard; (void)keys;
    nack__wl.last_serial = serial;

    if (!surface)
        return;
    for (size_t i = 0; i < nack__g.window_count; ++i) {
        struct nack_window *w = nack__g.windows[i];
        struct nack_wl_window *ww = nack__wl_win(w);
        if (ww && ww->surface == surface) {
            nack__wl.keyboard_focus = w;
            nack__emit_focus(w, true);
            return;
        }
    }
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface)
{
    (void)data; (void)keyboard; (void)surface;
    nack__wl.last_serial = serial;
    nack__wl_stop_repeat();
    if (nack__wl.compose_state)
        xkb_compose_state_reset(nack__wl.compose_state);
    if (nack__wl.keyboard_focus) {
        nack__emit_focus(nack__wl.keyboard_focus, false);
        nack__wl.keyboard_focus = NULL;
    }
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state)
{
    (void)data; (void)keyboard; (void)time;
    nack__wl.last_serial = serial;

    struct nack_window *w = nack__wl.keyboard_focus;
    if (!w)
        return;

    xkb_keycode_t keycode = NACK_WL_KEYCODE(key);
    enum nack_key nkey = keycode < 256 ? nack__wl.keycodes[keycode] : NACK_KEY_UNKNOWN;
    uint32_t mods = nack__wl_mods();
    bool down = (state == WL_KEYBOARD_KEY_STATE_PRESSED);

    nack__emit_key(w, nkey, key, mods, down, false);

    if (down) {
        nack__wl_emit_text_for(w, keycode, mods);
        nack__wl_start_repeat(w, key);
    } else if (nack__wl.repeat_key == key + 1) {
        nack__wl_stop_repeat();
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group)
{
    (void)data; (void)keyboard;
    nack__wl.last_serial = serial;
    if (nack__wl.xkb_state)
        xkb_state_update_mask(nack__wl.xkb_state, mods_depressed, mods_latched,
                              mods_locked, 0, 0, group);
    nack__g.mods = nack__wl_mods();
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay)
{
    (void)data; (void)keyboard;
    nack__wl.repeat_rate = rate;
    nack__wl.repeat_delay = delay;
    if (rate == 0)
        nack__wl_stop_repeat();   /* the user disabled repeat */
}

static const struct wl_keyboard_listener nack__wl_keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

/* ------------------------------------------------------------------ */
/* Cursors                                                            */
/* ------------------------------------------------------------------ */

static const char *const nack__wl_cursor_names[NACK_CURSOR_SHAPE_COUNT][3] = {
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

void nack__wl_load_cursor_theme(int scale)
{
    if (!nack__wl.shm || scale < 1)
        return;
    if (nack__wl.cursor_theme && nack__wl.cursor_theme_scale == scale)
        return;

    int size = 24;
    const char *size_env = getenv("XCURSOR_SIZE");
    if (size_env && *size_env) {
        int parsed = atoi(size_env);
        if (parsed > 0)
            size = parsed;
    }

    struct wl_cursor_theme *theme =
        wl_cursor_theme_load(getenv("XCURSOR_THEME"), size * scale, nack__wl.shm);
    if (!theme)
        return;

    if (nack__wl.cursor_theme)
        wl_cursor_theme_destroy(nack__wl.cursor_theme);
    nack__wl.cursor_theme = theme;
    nack__wl.cursor_theme_scale = scale;
    memset(nack__wl.cursors, 0, sizeof nack__wl.cursors);
    memset(nack__wl.cursors_loaded, 0, sizeof nack__wl.cursors_loaded);
}

static struct wl_cursor *nack__wl_get_cursor(enum nack_cursor_shape shape)
{
    if (nack__wl.cursors_loaded[shape])
        return nack__wl.cursors[shape];
    struct wl_cursor *cursor = NULL;
    if (nack__wl.cursor_theme) {
        for (int i = 0; i < 3 && nack__wl_cursor_names[shape][i]; ++i) {
            cursor = wl_cursor_theme_get_cursor(nack__wl.cursor_theme,
                                                nack__wl_cursor_names[shape][i]);
            if (cursor)
                break;
        }
    }
    nack__wl.cursors[shape] = cursor;
    nack__wl.cursors_loaded[shape] = true;
    return cursor;
}

void nack__wl_update_cursor(struct nack_window *w)
{
    if (!nack__wl.pointer || nack__wl.pointer_focus != w)
        return;

    if (w->cursor_mode != NACK_CURSOR_MODE_NORMAL) {
        /* A null surface hides the pointer for this surface. */
        wl_pointer_set_cursor(nack__wl.pointer, nack__wl.pointer_enter_serial,
                              NULL, 0, 0);
        return;
    }

    struct wl_cursor *cursor = nack__wl_get_cursor(w->cursor_shape);
    if (!cursor || !nack__wl.cursor_surface || cursor->image_count == 0)
        return;

    struct wl_cursor_image *image = cursor->images[0];
    struct wl_buffer *buffer = wl_cursor_image_get_buffer(image);
    if (!buffer)
        return;

    int scale = nack__wl.cursor_theme_scale > 0 ? nack__wl.cursor_theme_scale : 1;
    wl_surface_set_buffer_scale(nack__wl.cursor_surface, scale);
    wl_surface_attach(nack__wl.cursor_surface, buffer, 0, 0);
    wl_surface_damage_buffer(nack__wl.cursor_surface, 0, 0,
                             (int32_t)image->width, (int32_t)image->height);
    wl_surface_commit(nack__wl.cursor_surface);

    wl_pointer_set_cursor(nack__wl.pointer, nack__wl.pointer_enter_serial,
                          nack__wl.cursor_surface,
                          (int32_t)image->hotspot_x / scale,
                          (int32_t)image->hotspot_y / scale);
}

void nack__wl_set_cursor_shape(struct nack_window *w, enum nack_cursor_shape shape)
{
    (void)shape;
    nack__wl_update_cursor(w);
}

/* ------------------------------------------------------------------ */
/* Relative pointer (captured cursor mode)                            */
/* ------------------------------------------------------------------ */

static void relative_pointer_motion(void *data,
                                    struct zwp_relative_pointer_v1 *relative,
                                    uint32_t utime_hi, uint32_t utime_lo,
                                    wl_fixed_t dx, wl_fixed_t dy,
                                    wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel)
{
    (void)relative; (void)utime_hi; (void)utime_lo;
    (void)dx_unaccel; (void)dy_unaccel;

    struct nack_window *w = (struct nack_window *)data;
    if (w->cursor_mode != NACK_CURSOR_MODE_CAPTURED)
        return;

    struct nack_wl_window *ww = nack__wl_win(w);
    double ddx = wl_fixed_to_double(dx);
    double ddy = wl_fixed_to_double(dy);
    ww->virtual_x += ddx;
    ww->virtual_y += ddy;

    struct nack_event *ev = nack__event_begin(NACK_EVENT_MOUSE_MOVE, w);
    ev->data.motion.x = ww->virtual_x;
    ev->data.motion.y = ww->virtual_y;
    ev->data.motion.dx = ddx;
    ev->data.motion.dy = ddy;
    ev->data.motion.mods = nack__wl_mods();
    nack__push_event(ev);
}

static const struct zwp_relative_pointer_v1_listener nack__wl_relative_listener = {
    .relative_motion = relative_pointer_motion,
};

void nack__wl_set_cursor_mode(struct nack_window *w, enum nack_cursor_mode mode)
{
    struct nack_wl_window *ww = nack__wl_win(w);

    if (mode == NACK_CURSOR_MODE_CAPTURED) {
        ww->virtual_x = w->mouse_x;
        ww->virtual_y = w->mouse_y;

        /*
         * Wayland has no pointer warping, so relative motion comes from
         * zwp_relative_pointer_v1 while zwp_locked_pointer_v1 keeps the
         * pointer inside the surface.
         */
        if (nack__wl.relative_pointer_manager && nack__wl.pointer &&
            !ww->relative_pointer) {
            ww->relative_pointer =
                zwp_relative_pointer_manager_v1_get_relative_pointer(
                    nack__wl.relative_pointer_manager, nack__wl.pointer);
            zwp_relative_pointer_v1_add_listener(ww->relative_pointer,
                                                 &nack__wl_relative_listener, w);
        }
        if (nack__wl.pointer_constraints && nack__wl.pointer && !ww->locked_pointer) {
            ww->locked_pointer = zwp_pointer_constraints_v1_lock_pointer(
                nack__wl.pointer_constraints, ww->surface, nack__wl.pointer, NULL,
                ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        }
        if (!ww->relative_pointer || !ww->locked_pointer)
            nack__log("nack: compositor lacks pointer constraints; "
                      "captured cursor mode is approximate");
    } else {
        if (ww->locked_pointer) {
            zwp_locked_pointer_v1_destroy(ww->locked_pointer);
            ww->locked_pointer = NULL;
        }
        if (ww->relative_pointer) {
            zwp_relative_pointer_v1_destroy(ww->relative_pointer);
            ww->relative_pointer = NULL;
        }
    }

    nack__wl_update_cursor(w);
    wl_display_flush(nack__wl.display);
}

/* ------------------------------------------------------------------ */
/* Pointer                                                            */
/* ------------------------------------------------------------------ */

static struct nack_window *nack__wl_window_for_surface(struct wl_surface *surface)
{
    if (!surface)
        return NULL;
    for (size_t i = 0; i < nack__g.window_count; ++i) {
        struct nack_window *w = nack__g.windows[i];
        struct nack_wl_window *ww = nack__wl_win(w);
        if (ww && ww->surface == surface)
            return w;
    }
    return NULL;
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data; (void)pointer;
    nack__wl.pointer_enter_serial = serial;
    nack__wl.last_serial = serial;

    struct nack_window *w = nack__wl_window_for_surface(surface);
    if (!w) {
        /* The pointer may instead be over one of our decoration surfaces. */
        enum nack_wl_decor_part part;
        if (nack__wl_decor_find(surface, &w, &part)) {
            nack__wl.decor_focus = w;
            nack__wl.decor_focus_part = part;
            nack__wl_decor_pointer_motion(w, wl_fixed_to_double(sx),
                                          wl_fixed_to_double(sy));
        }
        return;
    }

    nack__wl.decor_focus = NULL;
    nack__wl.pointer_focus = w;
    w->mouse_x = wl_fixed_to_double(sx);
    w->mouse_y = wl_fixed_to_double(sy);
    nack__wl_update_cursor(w);
    nack__emit_simple(w, NACK_EVENT_MOUSE_ENTER);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface)
{
    (void)data; (void)pointer; (void)surface;
    nack__wl.last_serial = serial;
    if (nack__wl.decor_focus) {
        nack__wl_decor_pointer_leave(nack__wl.decor_focus);
        nack__wl.decor_focus = NULL;
    }
    if (nack__wl.pointer_focus) {
        nack__emit_simple(nack__wl.pointer_focus, NACK_EVENT_MOUSE_LEAVE);
        nack__wl.pointer_focus = NULL;
    }
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data; (void)pointer; (void)time;

    if (nack__wl.decor_focus) {
        nack__wl_decor_pointer_motion(nack__wl.decor_focus, wl_fixed_to_double(sx),
                                      wl_fixed_to_double(sy));
        return;
    }

    struct nack_window *w = nack__wl.pointer_focus;
    if (!w || w->cursor_mode == NACK_CURSOR_MODE_CAPTURED)
        return;   /* captured motion arrives through the relative pointer */
    nack__emit_mouse_move(w, wl_fixed_to_double(sx), wl_fixed_to_double(sy),
                          nack__wl_mods());
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state)
{
    (void)data; (void)pointer; (void)time;
    nack__wl.last_serial = serial;

    struct nack_window *w = nack__wl.pointer_focus;

    /* Wayland reports Linux input event codes. */
    int index;
    switch (button) {
    case 0x110: index = NACK_MOUSE_LEFT; break;    /* BTN_LEFT   */
    case 0x111: index = NACK_MOUSE_RIGHT; break;   /* BTN_RIGHT  */
    case 0x112: index = NACK_MOUSE_MIDDLE; break;  /* BTN_MIDDLE */
    case 0x113: index = NACK_MOUSE_X1; break;      /* BTN_SIDE   */
    case 0x114: index = NACK_MOUSE_X2; break;      /* BTN_EXTRA  */
    default:    index = (int)(button - 0x110); break;
    }

    bool down = (state == WL_POINTER_BUTTON_STATE_PRESSED);

    if (nack__wl.decor_focus) {
        /* Route through the multi-click tracker so a double-click on the
         * title bar is recognised, then let the decorations act on it. */
        struct nack_window *decorated = nack__wl.decor_focus;
        if (down)
            nack__emit_mouse_button(decorated, index, true, nack__wl.decor_x,
                                    nack__wl.decor_y, nack__wl_mods());
        if (nack__wl_decor_pointer_button(decorated, index, down, serial))
            return;
        return;
    }

    if (!w)
        return;

    nack__emit_mouse_button(w, index, down, w->mouse_x, w->mouse_y, nack__wl_mods());
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time;
    if (!nack__wl.pointer_focus)
        return;

    /*
     * Axis values are in surface-local units, where roughly 10 corresponds to
     * one wheel detent. Normalise to detents and let the frame event decide
     * whether this was a discrete wheel or a continuous trackpad gesture.
     */
    double amount = wl_fixed_to_double(value) / 10.0;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        nack__wl.axis_y -= amount;
    else
        nack__wl.axis_x -= amount;
    nack__wl.axis_pending = true;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                                  int32_t discrete)
{
    (void)data; (void)pointer; (void)axis; (void)discrete;
    nack__wl.axis_discrete = true;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source)
{
    (void)data; (void)pointer;
    if (source == WL_POINTER_AXIS_SOURCE_WHEEL)
        nack__wl.axis_discrete = true;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                              uint32_t axis)
{
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data; (void)pointer;
    if (!nack__wl.axis_pending) {
        nack__wl.axis_discrete = false;
        return;
    }
    struct nack_window *w = nack__wl.pointer_focus;
    if (w)
        nack__emit_scroll(w, nack__wl.axis_x, nack__wl.axis_y, nack__wl_mods(),
                          !nack__wl.axis_discrete);
    nack__wl.axis_x = 0.0;
    nack__wl.axis_y = 0.0;
    nack__wl.axis_pending = false;
    nack__wl.axis_discrete = false;
}

static const struct wl_pointer_listener nack__wl_pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

/* ------------------------------------------------------------------ */
/* Seat                                                               */
/* ------------------------------------------------------------------ */

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    (void)data;

    bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

    if (has_pointer && !nack__wl.pointer) {
        nack__wl.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(nack__wl.pointer, &nack__wl_pointer_listener, NULL);
    } else if (!has_pointer && nack__wl.pointer) {
        wl_pointer_release(nack__wl.pointer);
        nack__wl.pointer = NULL;
    }

    if (has_keyboard && !nack__wl.keyboard) {
        nack__wl.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(nack__wl.keyboard, &nack__wl_keyboard_listener, NULL);
    } else if (!has_keyboard && nack__wl.keyboard) {
        wl_keyboard_release(nack__wl.keyboard);
        nack__wl.keyboard = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener nack__wl_seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

void nack__wl_seat_bind(uint32_t name, uint32_t version)
{
    if (nack__wl.seat)
        return;

    nack__wl.seat = wl_registry_bind(nack__wl.registry, name, &wl_seat_interface,
                                     version);
    wl_seat_add_listener(nack__wl.seat, &nack__wl_seat_listener, NULL);

    if (!nack__wl.xkb_context) {
        nack__wl.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (nack__wl.xkb_context) {
            const char *locale = getenv("LC_ALL");
            if (!locale || !*locale) locale = getenv("LC_CTYPE");
            if (!locale || !*locale) locale = getenv("LANG");
            if (!locale || !*locale) locale = "C";
            nack__wl.compose_table = xkb_compose_table_new_from_locale(
                nack__wl.xkb_context, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
            if (nack__wl.compose_table)
                nack__wl.compose_state = xkb_compose_state_new(
                    nack__wl.compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
        }
    }
}

void nack__wl_input_shutdown(void)
{
    if (nack__wl.pointer)  wl_pointer_release(nack__wl.pointer);
    if (nack__wl.keyboard) wl_keyboard_release(nack__wl.keyboard);
    if (nack__wl.touch)    wl_touch_release(nack__wl.touch);
    if (nack__wl.seat)     wl_seat_destroy(nack__wl.seat);
    nack__wl.pointer = NULL;
    nack__wl.keyboard = NULL;
    nack__wl.touch = NULL;
    nack__wl.seat = NULL;

    if (nack__wl.compose_state) xkb_compose_state_unref(nack__wl.compose_state);
    if (nack__wl.compose_table) xkb_compose_table_unref(nack__wl.compose_table);
    if (nack__wl.xkb_state)     xkb_state_unref(nack__wl.xkb_state);
    if (nack__wl.xkb_keymap)    xkb_keymap_unref(nack__wl.xkb_keymap);
    if (nack__wl.xkb_context)   xkb_context_unref(nack__wl.xkb_context);
    nack__wl.compose_state = NULL;
    nack__wl.compose_table = NULL;
    nack__wl.xkb_state = NULL;
    nack__wl.xkb_keymap = NULL;
    nack__wl.xkb_context = NULL;
}

void nack__wl_seat_release(void)
{
    nack__wl_input_shutdown();
}
