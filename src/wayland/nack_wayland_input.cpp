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

namespace nack { namespace detail {

static uint32_t wl_mods(void)
{
    if (!wl.xkb_state)
        return 0;
    struct xkb_state *xkb = wl.xkb_state;
    uint32_t mods = 0;
    if (xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_SHIFT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_SHIFT;
    if (xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_CTRL,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_CTRL;
    if (xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_ALT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_ALT;
    if (xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_LOGO,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_SUPER;
    if (xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_CAPS,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_CAPSLOCK;
    if (xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_NUM,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= NACK_MOD_NUMLOCK;
    return mods;
}

/* ------------------------------------------------------------------ */
/* Text production                                                    */
/* ------------------------------------------------------------------ */

static void wl_emit_text_for(nack_window *w, xkb_keycode_t keycode,
                                   uint32_t mods)
{
    if (!wl.xkb_state)
        return;

    xkb_keysym_t sym = xkb_state_key_get_one_sym(wl.xkb_state, keycode);

    if (wl.compose_state) {
        if (xkb_compose_state_feed(wl.compose_state, sym) ==
            XKB_COMPOSE_FEED_ACCEPTED) {
            switch (xkb_compose_state_get_status(wl.compose_state)) {
            case XKB_COMPOSE_COMPOSING:
                return;
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(wl.compose_state);
                return;
            case XKB_COMPOSE_COMPOSED: {
                char buffer[32];
                int n = xkb_compose_state_get_utf8(wl.compose_state, buffer,
                                                   sizeof buffer);
                xkb_compose_state_reset(wl.compose_state);
                if (n > 0 && n < (int)sizeof buffer)
                    w->emit_text(buffer);
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
    int n = xkb_state_key_get_utf8(wl.xkb_state, keycode, buffer, sizeof buffer);
    if (n <= 0 || n >= (int)sizeof buffer)
        return;

    uint32_t codepoint = xkb_state_key_get_utf32(wl.xkb_state, keycode);
    if (!codepoint_is_text(codepoint))
        return;

    w->emit_text(buffer);
}

/* ------------------------------------------------------------------ */
/* Key repeat                                                         */
/* ------------------------------------------------------------------ */

static void wl_start_repeat(nack_window *w, uint32_t raw_key)
{
    if (wl.repeat_rate <= 0 || !wl.xkb_keymap)
        return;
    if (!xkb_keymap_key_repeats(wl.xkb_keymap, NACK_WL_KEYCODE(raw_key)))
        return;
    wl.repeat_key = raw_key + 1;   /* +1 so 0 stays the idle marker */
    wl.repeat_window = w;
    wl.repeat_next_ns =
        win_time_ns() + (uint64_t)wl.repeat_delay * 1000000ull;
}

static void wl_stop_repeat(void)
{
    wl.repeat_key = 0;
    wl.repeat_window = nullptr;
}

double wl_next_repeat_timeout(void)
{
    if (!wl.repeat_key || !wl.repeat_window)
        return -1.0;
    uint64_t now = win_time_ns();
    if (now >= wl.repeat_next_ns)
        return 0.0;
    return (double)(wl.repeat_next_ns - now) / 1e9;
}

void wl_pump_key_repeat(void)
{
    if (!wl.repeat_key || !wl.repeat_window)
        return;

    uint64_t now = win_time_ns();
    uint64_t interval = wl.repeat_rate > 0
                            ? 1000000000ull / (uint64_t)wl.repeat_rate
                            : 0;
    if (interval == 0)
        return;

    /* Catch up without spinning if the caller was blocked for a long time. */
    int guard = 0;
    while (now >= wl.repeat_next_ns && guard++ < 32) {
        uint32_t raw_key = wl.repeat_key - 1;
        xkb_keycode_t keycode = NACK_WL_KEYCODE(raw_key);
        nack_key key = keycode < 256 ? wl.keycodes[keycode] : NACK_KEY_UNKNOWN;
        uint32_t mods = wl_mods();

        wl.repeat_window->emit_key(key, raw_key, mods, true, true);
        wl_emit_text_for(wl.repeat_window, keycode, mods);

        wl.repeat_next_ns += interval;
    }
    if (guard >= 32)
        wl.repeat_next_ns = now + interval;
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

    char *map = (char *)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_string(
        wl.xkb_context, map, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);

    if (!keymap)
        return;

    struct xkb_state *xkb = xkb_state_new(keymap);
    if (!xkb) {
        xkb_keymap_unref(keymap);
        return;
    }

    if (wl.xkb_state)  xkb_state_unref(wl.xkb_state);
    if (wl.xkb_keymap) xkb_keymap_unref(wl.xkb_keymap);
    wl.xkb_keymap = keymap;
    wl.xkb_state = xkb;
    xkb_build_keycodes(wl.xkb_keymap, wl.keycodes);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys)
{
    (void)data; (void)keyboard; (void)keys;
    wl.last_serial = serial;

    if (!surface)
        return;
    for (size_t i = 0; i < state.windows.size(); ++i) {
        nack_window *w = state.windows[i];
        nack_wl_window *ww = wl_win(w);
        if (ww && ww->surface == surface) {
            wl.keyboard_focus = w;
            w->emit_focus(true);
            /* The title bar is drawn differently when active. */
            wl_decor_redraw(w);
            return;
        }
    }
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface)
{
    (void)data; (void)keyboard; (void)surface;
    wl.last_serial = serial;
    wl_stop_repeat();
    if (wl.compose_state)
        xkb_compose_state_reset(wl.compose_state);
    if (wl.keyboard_focus) {
        nack_window *w = wl.keyboard_focus;
        wl.keyboard_focus = nullptr;
        w->emit_focus(false);
        wl_decor_redraw(w);
    }
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t key_state)
{
    (void)data; (void)keyboard; (void)time;
    wl.last_serial = serial;

    nack_window *w = wl.keyboard_focus;
    if (!w)
        return;

    xkb_keycode_t keycode = NACK_WL_KEYCODE(key);
    nack_key nkey = keycode < 256 ? wl.keycodes[keycode] : NACK_KEY_UNKNOWN;
    uint32_t mods = wl_mods();
    bool down = (key_state == WL_KEYBOARD_KEY_STATE_PRESSED);

    w->emit_key(nkey, key, mods, down, false);

    if (down) {
        wl_emit_text_for(w, keycode, mods);
        wl_start_repeat(w, key);
    } else if (wl.repeat_key == key + 1) {
        wl_stop_repeat();
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group)
{
    (void)data; (void)keyboard;
    wl.last_serial = serial;
    if (wl.xkb_state)
        xkb_state_update_mask(wl.xkb_state, mods_depressed, mods_latched,
                              mods_locked, 0, 0, group);
    state.mods = wl_mods();
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay)
{
    (void)data; (void)keyboard;
    wl.repeat_rate = rate;
    wl.repeat_delay = delay;
    if (rate == 0)
        wl_stop_repeat();   /* the user disabled repeat */
}

static const struct wl_keyboard_listener wl_keyboard_listener = {
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

static const char *const wl_cursor_names[NACK_CURSOR_SHAPE_COUNT][3] = {
    { "default", "left_ptr", nullptr },
    { "text", "xterm", nullptr },
    { "crosshair", "cross", nullptr },
    { "pointer", "hand2", nullptr },
    { "ew-resize", "sb_h_double_arrow", nullptr },
    { "ns-resize", "sb_v_double_arrow", nullptr },
    { "nwse-resize", "size_fdiag", nullptr },
    { "nesw-resize", "size_bdiag", nullptr },
    { "all-scroll", "fleur", nullptr },
    { "not-allowed", "crossed_circle", nullptr },
    { "wait", "watch", nullptr },
};

void wl_load_cursor_theme(int scale)
{
    if (!wl.shm || scale < 1)
        return;
    if (wl.cursor_theme && wl.cursor_theme_scale == scale)
        return;

    int size = 24;
    const char *size_env = getenv("XCURSOR_SIZE");
    if (size_env && *size_env) {
        int parsed = atoi(size_env);
        if (parsed > 0)
            size = parsed;
    }

    struct wl_cursor_theme *theme =
        wl_cursor_theme_load(getenv("XCURSOR_THEME"), size * scale, wl.shm);
    if (!theme)
        return;

    if (wl.cursor_theme)
        wl_cursor_theme_destroy(wl.cursor_theme);
    wl.cursor_theme = theme;
    wl.cursor_theme_scale = scale;
    memset(wl.cursors, 0, sizeof wl.cursors);
    memset(wl.cursors_loaded, 0, sizeof wl.cursors_loaded);
}

static struct wl_cursor *wl_get_cursor(nack_cursor_shape shape)
{
    if (wl.cursors_loaded[shape])
        return wl.cursors[shape];
    struct wl_cursor *cursor = nullptr;
    if (wl.cursor_theme) {
        for (int i = 0; i < 3 && wl_cursor_names[shape][i]; ++i) {
            cursor = wl_cursor_theme_get_cursor(wl.cursor_theme,
                                                wl_cursor_names[shape][i]);
            if (cursor)
                break;
        }
    }
    wl.cursors[shape] = cursor;
    wl.cursors_loaded[shape] = true;
    return cursor;
}

void wl_apply_cursor_shape(nack_cursor_shape shape)
{
    struct wl_cursor *cursor;
    struct wl_cursor_image *image;
    struct wl_buffer *buffer;
    int scale;

    if (!wl.pointer)
        return;

    cursor = wl_get_cursor(shape);
    if (!cursor || !wl.cursor_surface || cursor->image_count == 0)
        return;

    image = cursor->images[0];
    buffer = wl_cursor_image_get_buffer(image);
    if (!buffer)
        return;

    scale = wl.cursor_theme_scale > 0 ? wl.cursor_theme_scale : 1;
    wl_surface_set_buffer_scale(wl.cursor_surface, scale);
    wl_surface_attach(wl.cursor_surface, buffer, 0, 0);
    wl_surface_damage_buffer(wl.cursor_surface, 0, 0,
                             (int32_t)image->width, (int32_t)image->height);
    wl_surface_commit(wl.cursor_surface);

    wl_pointer_set_cursor(wl.pointer, wl.pointer_enter_serial,
                          wl.cursor_surface,
                          (int32_t)image->hotspot_x / scale,
                          (int32_t)image->hotspot_y / scale);
}

void wl_update_cursor(nack_window *w)
{
    if (!wl.pointer || wl.pointer_focus != w)
        return;

    if (w->cursor_mode != NACK_CURSOR_MODE_NORMAL) {
        /* A null surface hides the pointer over this surface. */
        wl_pointer_set_cursor(wl.pointer, wl.pointer_enter_serial,
                              nullptr, 0, 0);
        return;
    }

    wl_apply_cursor_shape(w->cursor_shape);
}

void wl_set_cursor_shape(nack_window *w, nack_cursor_shape shape)
{
    (void)shape;
    wl_update_cursor(w);
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

    nack_window *w = (nack_window *)data;
    if (w->cursor_mode != NACK_CURSOR_MODE_CAPTURED)
        return;

    nack_wl_window *ww = wl_win(w);
    double ddx = wl_fixed_to_double(dx);
    double ddy = wl_fixed_to_double(dy);
    ww->virtual_x += ddx;
    ww->virtual_y += ddy;

    nack_win_event *ev = state.event_begin(NACK_WIN_EVENT_MOUSE_MOVE, w);
    ev->data.motion.x = ww->virtual_x;
    ev->data.motion.y = ww->virtual_y;
    ev->data.motion.dx = ddx;
    ev->data.motion.dy = ddy;
    ev->data.motion.mods = wl_mods();
    state.push_event(ev);
}

static const struct zwp_relative_pointer_v1_listener wl_relative_listener = {
    .relative_motion = relative_pointer_motion,
};

void wl_set_cursor_mode(nack_window *w, nack_cursor_mode mode)
{
    nack_wl_window *ww = wl_win(w);

    if (mode == NACK_CURSOR_MODE_CAPTURED) {
        ww->virtual_x = w->mouse_x;
        ww->virtual_y = w->mouse_y;

        /*
         * Wayland has no pointer warping, so relative motion comes from
         * zwp_relative_pointer_v1 while zwp_locked_pointer_v1 keeps the
         * pointer inside the surface.
         */
        if (wl.relative_pointer_manager && wl.pointer &&
            !ww->relative_pointer) {
            ww->relative_pointer =
                zwp_relative_pointer_manager_v1_get_relative_pointer(
                    wl.relative_pointer_manager, wl.pointer);
            zwp_relative_pointer_v1_add_listener(ww->relative_pointer,
                                                 &wl_relative_listener, w);
        }
        if (wl.pointer_constraints && wl.pointer && !ww->locked_pointer) {
            ww->locked_pointer = zwp_pointer_constraints_v1_lock_pointer(
                wl.pointer_constraints, ww->surface, wl.pointer, nullptr,
                ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        }
        if (!ww->relative_pointer || !ww->locked_pointer)
            nack_log("nack: compositor lacks pointer constraints; "
                      "captured cursor mode is approximate");
    } else {
        if (ww->locked_pointer) {
            zwp_locked_pointer_v1_destroy(ww->locked_pointer);
            ww->locked_pointer = nullptr;
        }
        if (ww->relative_pointer) {
            zwp_relative_pointer_v1_destroy(ww->relative_pointer);
            ww->relative_pointer = nullptr;
        }
    }

    wl_update_cursor(w);
    wl_display_flush(wl.display);
}

/* ------------------------------------------------------------------ */
/* Pointer                                                            */
/* ------------------------------------------------------------------ */

static nack_window *wl_window_for_surface(struct wl_surface *surface)
{
    if (!surface)
        return nullptr;
    for (size_t i = 0; i < state.windows.size(); ++i) {
        nack_window *w = state.windows[i];
        nack_wl_window *ww = wl_win(w);
        if (ww && ww->surface == surface)
            return w;
    }
    return nullptr;
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data; (void)pointer;
    wl.pointer_enter_serial = serial;
    wl.last_serial = serial;

    nack_window *w = wl_window_for_surface(surface);
    if (!w) {
        /* The pointer may instead be over one of our decoration surfaces. */
        nack_wl_decor_part part;
        if (wl_decor_find(surface, &w, &part)) {
            wl.decor_focus = w;
            wl.decor_focus_part = part;
            wl_decor_pointer_motion(w, wl_fixed_to_double(sx),
                                          wl_fixed_to_double(sy));
        }
        return;
    }

    wl.decor_focus = nullptr;
    wl.pointer_focus = w;
    w->mouse_x = wl_fixed_to_double(sx);
    w->mouse_y = wl_fixed_to_double(sy);
    wl_update_cursor(w);
    w->emit_simple(NACK_WIN_EVENT_MOUSE_ENTER);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface)
{
    (void)data; (void)pointer; (void)surface;
    wl.last_serial = serial;
    if (wl.decor_focus) {
        wl_decor_pointer_leave(wl.decor_focus);
        wl.decor_focus = nullptr;
    }
    if (wl.pointer_focus) {
        wl.pointer_focus->emit_simple(NACK_WIN_EVENT_MOUSE_LEAVE);
        wl.pointer_focus = nullptr;
    }
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data; (void)pointer; (void)time;

    if (wl.decor_focus) {
        wl_decor_pointer_motion(wl.decor_focus, wl_fixed_to_double(sx),
                                      wl_fixed_to_double(sy));
        return;
    }

    nack_window *w = wl.pointer_focus;
    if (!w || w->cursor_mode == NACK_CURSOR_MODE_CAPTURED)
        return;   /* captured motion arrives through the relative pointer */
    w->emit_mouse_move(wl_fixed_to_double(sx), wl_fixed_to_double(sy),
                      wl_mods());
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t button_state)
{
    (void)data; (void)pointer; (void)time;
    wl.last_serial = serial;

    nack_window *w = wl.pointer_focus;

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

    bool down = (button_state == WL_POINTER_BUTTON_STATE_PRESSED);

    if (wl.decor_focus) {
        /* Route through the multi-click tracker so a double-click on the
         * title bar is recognised, then let the decorations act on it. */
        nack_window *decorated = wl.decor_focus;
        if (down)
            decorated->emit_mouse_button(index, true, wl.decor_x,
                                         wl.decor_y, wl_mods());
        if (wl_decor_pointer_button(decorated, index, down, serial))
            return;
        return;
    }

    if (!w)
        return;

    w->emit_mouse_button(index, down, w->mouse_x, w->mouse_y, wl_mods());
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time;
    if (!wl.pointer_focus)
        return;

    /*
     * Axis values are in surface-local units, where roughly 10 corresponds to
     * one wheel detent. Normalise to detents and let the frame event decide
     * whether this was a discrete wheel or a continuous trackpad gesture.
     */
    double amount = wl_fixed_to_double(value) / 10.0;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        wl.axis_y -= amount;
    else
        wl.axis_x -= amount;
    wl.axis_pending = true;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                                  int32_t discrete)
{
    (void)data; (void)pointer; (void)axis; (void)discrete;
    wl.axis_discrete = true;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source)
{
    (void)data; (void)pointer;
    if (source == WL_POINTER_AXIS_SOURCE_WHEEL)
        wl.axis_discrete = true;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                              uint32_t axis)
{
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data; (void)pointer;
    if (!wl.axis_pending) {
        wl.axis_discrete = false;
        return;
    }
    nack_window *w = wl.pointer_focus;
    if (w)
        w->emit_scroll(wl.axis_x, wl.axis_y, wl_mods(),
                      !wl.axis_discrete);
    wl.axis_x = 0.0;
    wl.axis_y = 0.0;
    wl.axis_pending = false;
    wl.axis_discrete = false;
}

static const struct wl_pointer_listener wl_pointer_listener = {
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

    if (has_pointer && !wl.pointer) {
        wl.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(wl.pointer, &wl_pointer_listener, nullptr);
    } else if (!has_pointer && wl.pointer) {
        wl_pointer_release(wl.pointer);
        wl.pointer = nullptr;
    }

    if (has_keyboard && !wl.keyboard) {
        wl.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(wl.keyboard, &wl_keyboard_listener, nullptr);
    } else if (!has_keyboard && wl.keyboard) {
        wl_keyboard_release(wl.keyboard);
        wl.keyboard = nullptr;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener wl_seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

void wl_seat_bind(uint32_t name, uint32_t version)
{
    if (wl.seat)
        return;

    wl.seat = (struct wl_seat *)wl_registry_bind(wl.registry, name, &wl_seat_interface,
                                     version);
    wl_seat_add_listener(wl.seat, &wl_seat_listener, nullptr);

    if (!wl.xkb_context) {
        wl.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (wl.xkb_context) {
            const char *locale = getenv("LC_ALL");
            if (!locale || !*locale) locale = getenv("LC_CTYPE");
            if (!locale || !*locale) locale = getenv("LANG");
            if (!locale || !*locale) locale = "C";
            wl.compose_table = xkb_compose_table_new_from_locale(
                wl.xkb_context, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
            if (wl.compose_table)
                wl.compose_state = xkb_compose_state_new(
                    wl.compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
        }
    }
}

void wl_input_shutdown(void)
{
    if (wl.pointer)  wl_pointer_release(wl.pointer);
    if (wl.keyboard) wl_keyboard_release(wl.keyboard);
    if (wl.touch)    wl_touch_release(wl.touch);
    if (wl.seat)     wl_seat_destroy(wl.seat);
    wl.pointer = nullptr;
    wl.keyboard = nullptr;
    wl.touch = nullptr;
    wl.seat = nullptr;

    if (wl.compose_state) xkb_compose_state_unref(wl.compose_state);
    if (wl.compose_table) xkb_compose_table_unref(wl.compose_table);
    if (wl.xkb_state)     xkb_state_unref(wl.xkb_state);
    if (wl.xkb_keymap)    xkb_keymap_unref(wl.xkb_keymap);
    if (wl.xkb_context)   xkb_context_unref(wl.xkb_context);
    wl.compose_state = nullptr;
    wl.compose_table = nullptr;
    wl.xkb_state = nullptr;
    wl.xkb_keymap = nullptr;
    wl.xkb_context = nullptr;
}

void wl_seat_unbind(void)
{
    wl_input_shutdown();
}

} }   /* namespace nack::detail */
