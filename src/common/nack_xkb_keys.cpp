/*
 * Shared xkbcommon keycode mapping for the XCB and Wayland backends.
 *
 * Physical keys are identified by their XKB key name ("AD01" and friends),
 * which is layout independent and exactly what a terminal wants. Not every
 * keymap carries names, though: virtual keyboards (wtype, ydotool, on-screen
 * keyboards, remote desktop bridges) frequently upload a keymap built from
 * keysyms alone. Falling back to the keysym keeps those usable instead of
 * reporting every key as unknown.
 */
#include "nack_xkb_keys.h"

static const struct { const char *name; nack_key key; } nack__xkb_names[] = {
    { "TLDE", NACK_KEY_GRAVE },      { "AE01", NACK_KEY_1 },
    { "AE02", NACK_KEY_2 },          { "AE03", NACK_KEY_3 },
    { "AE04", NACK_KEY_4 },          { "AE05", NACK_KEY_5 },
    { "AE06", NACK_KEY_6 },          { "AE07", NACK_KEY_7 },
    { "AE08", NACK_KEY_8 },          { "AE09", NACK_KEY_9 },
    { "AE10", NACK_KEY_0 },          { "AE11", NACK_KEY_MINUS },
    { "AE12", NACK_KEY_EQUAL },      { "BKSP", NACK_KEY_BACKSPACE },
    { "TAB",  NACK_KEY_TAB },        { "AD01", NACK_KEY_Q },
    { "AD02", NACK_KEY_W },          { "AD03", NACK_KEY_E },
    { "AD04", NACK_KEY_R },          { "AD05", NACK_KEY_T },
    { "AD06", NACK_KEY_Y },          { "AD07", NACK_KEY_U },
    { "AD08", NACK_KEY_I },          { "AD09", NACK_KEY_O },
    { "AD10", NACK_KEY_P },          { "AD11", NACK_KEY_LEFT_BRACKET },
    { "AD12", NACK_KEY_RIGHT_BRACKET }, { "BKSL", NACK_KEY_BACKSLASH },
    { "RTRN", NACK_KEY_ENTER },      { "CAPS", NACK_KEY_CAPS_LOCK },
    { "AC01", NACK_KEY_A },          { "AC02", NACK_KEY_S },
    { "AC03", NACK_KEY_D },          { "AC04", NACK_KEY_F },
    { "AC05", NACK_KEY_G },          { "AC06", NACK_KEY_H },
    { "AC07", NACK_KEY_J },          { "AC08", NACK_KEY_K },
    { "AC09", NACK_KEY_L },          { "AC10", NACK_KEY_SEMICOLON },
    { "AC11", NACK_KEY_APOSTROPHE }, { "LFSH", NACK_KEY_LEFT_SHIFT },
    { "LSGT", NACK_KEY_NON_US_BACKSLASH },
    { "AB01", NACK_KEY_Z },          { "AB02", NACK_KEY_X },
    { "AB03", NACK_KEY_C },          { "AB04", NACK_KEY_V },
    { "AB05", NACK_KEY_B },          { "AB06", NACK_KEY_N },
    { "AB07", NACK_KEY_M },          { "AB08", NACK_KEY_COMMA },
    { "AB09", NACK_KEY_PERIOD },     { "AB10", NACK_KEY_SLASH },
    { "RTSH", NACK_KEY_RIGHT_SHIFT },{ "LCTL", NACK_KEY_LEFT_CTRL },
    { "LWIN", NACK_KEY_LEFT_SUPER }, { "LALT", NACK_KEY_LEFT_ALT },
    { "SPCE", NACK_KEY_SPACE },      { "RALT", NACK_KEY_RIGHT_ALT },
    { "RWIN", NACK_KEY_RIGHT_SUPER },{ "RCTL", NACK_KEY_RIGHT_CTRL },
    { "MENU", NACK_KEY_MENU },       { "COMP", NACK_KEY_APPLICATION },
    { "ESC",  NACK_KEY_ESCAPE },
    { "FK01", NACK_KEY_F1 },  { "FK02", NACK_KEY_F2 },  { "FK03", NACK_KEY_F3 },
    { "FK04", NACK_KEY_F4 },  { "FK05", NACK_KEY_F5 },  { "FK06", NACK_KEY_F6 },
    { "FK07", NACK_KEY_F7 },  { "FK08", NACK_KEY_F8 },  { "FK09", NACK_KEY_F9 },
    { "FK10", NACK_KEY_F10 }, { "FK11", NACK_KEY_F11 }, { "FK12", NACK_KEY_F12 },
    { "FK13", NACK_KEY_F13 }, { "FK14", NACK_KEY_F14 }, { "FK15", NACK_KEY_F15 },
    { "FK16", NACK_KEY_F16 }, { "FK17", NACK_KEY_F17 }, { "FK18", NACK_KEY_F18 },
    { "FK19", NACK_KEY_F19 }, { "FK20", NACK_KEY_F20 }, { "FK21", NACK_KEY_F21 },
    { "FK22", NACK_KEY_F22 }, { "FK23", NACK_KEY_F23 }, { "FK24", NACK_KEY_F24 },
    { "PRSC", NACK_KEY_PRINT_SCREEN }, { "SCLK", NACK_KEY_SCROLL_LOCK },
    { "PAUS", NACK_KEY_PAUSE },      { "INS",  NACK_KEY_INSERT },
    { "HOME", NACK_KEY_HOME },       { "PGUP", NACK_KEY_PAGE_UP },
    { "DELE", NACK_KEY_DELETE },     { "END",  NACK_KEY_END },
    { "PGDN", NACK_KEY_PAGE_DOWN },  { "UP",   NACK_KEY_UP },
    { "LEFT", NACK_KEY_LEFT },       { "DOWN", NACK_KEY_DOWN },
    { "RGHT", NACK_KEY_RIGHT },      { "NMLK", NACK_KEY_NUM_LOCK },
    { "KPDV", NACK_KEY_KP_DIVIDE },  { "KPMU", NACK_KEY_KP_MULTIPLY },
    { "KPSU", NACK_KEY_KP_SUBTRACT },{ "KPAD", NACK_KEY_KP_ADD },
    { "KPEN", NACK_KEY_KP_ENTER },   { "KPEQ", NACK_KEY_KP_EQUAL },
    { "KP1",  NACK_KEY_KP_1 }, { "KP2", NACK_KEY_KP_2 }, { "KP3", NACK_KEY_KP_3 },
    { "KP4",  NACK_KEY_KP_4 }, { "KP5", NACK_KEY_KP_5 }, { "KP6", NACK_KEY_KP_6 },
    { "KP7",  NACK_KEY_KP_7 }, { "KP8", NACK_KEY_KP_8 }, { "KP9", NACK_KEY_KP_9 },
    { "KP0",  NACK_KEY_KP_0 }, { "KPDL", NACK_KEY_KP_DECIMAL },
    { "MUTE", NACK_KEY_MUTE }, { "VOL-", NACK_KEY_VOLUME_DOWN },
    { "VOL+", NACK_KEY_VOLUME_UP },
    { nullptr, NACK_KEY_UNKNOWN }
};

nack_key nack__key_from_keysym(uint32_t sym)
{
    switch (sym) {
    case XKB_KEY_Escape:        return NACK_KEY_ESCAPE;
    case XKB_KEY_Return:        return NACK_KEY_ENTER;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab:  return NACK_KEY_TAB;
    case XKB_KEY_BackSpace:     return NACK_KEY_BACKSPACE;
    case XKB_KEY_Delete:        return NACK_KEY_DELETE;
    case XKB_KEY_space:         return NACK_KEY_SPACE;
    case XKB_KEY_Left:          return NACK_KEY_LEFT;
    case XKB_KEY_Right:         return NACK_KEY_RIGHT;
    case XKB_KEY_Up:            return NACK_KEY_UP;
    case XKB_KEY_Down:          return NACK_KEY_DOWN;
    case XKB_KEY_Home:          return NACK_KEY_HOME;
    case XKB_KEY_End:           return NACK_KEY_END;
    case XKB_KEY_Page_Up:       return NACK_KEY_PAGE_UP;
    case XKB_KEY_Page_Down:     return NACK_KEY_PAGE_DOWN;
    case XKB_KEY_Insert:        return NACK_KEY_INSERT;
    case XKB_KEY_Shift_L:       return NACK_KEY_LEFT_SHIFT;
    case XKB_KEY_Shift_R:       return NACK_KEY_RIGHT_SHIFT;
    case XKB_KEY_Control_L:     return NACK_KEY_LEFT_CTRL;
    case XKB_KEY_Control_R:     return NACK_KEY_RIGHT_CTRL;
    case XKB_KEY_Alt_L:         return NACK_KEY_LEFT_ALT;
    case XKB_KEY_Alt_R:
    case XKB_KEY_ISO_Level3_Shift: return NACK_KEY_RIGHT_ALT;
    case XKB_KEY_Super_L:       return NACK_KEY_LEFT_SUPER;
    case XKB_KEY_Super_R:       return NACK_KEY_RIGHT_SUPER;
    case XKB_KEY_Caps_Lock:     return NACK_KEY_CAPS_LOCK;
    case XKB_KEY_Num_Lock:      return NACK_KEY_NUM_LOCK;
    case XKB_KEY_Scroll_Lock:   return NACK_KEY_SCROLL_LOCK;
    case XKB_KEY_Print:         return NACK_KEY_PRINT_SCREEN;
    case XKB_KEY_Pause:         return NACK_KEY_PAUSE;
    case XKB_KEY_Menu:          return NACK_KEY_MENU;
    case XKB_KEY_grave:         return NACK_KEY_GRAVE;
    case XKB_KEY_minus:         return NACK_KEY_MINUS;
    case XKB_KEY_equal:         return NACK_KEY_EQUAL;
    case XKB_KEY_bracketleft:   return NACK_KEY_LEFT_BRACKET;
    case XKB_KEY_bracketright:  return NACK_KEY_RIGHT_BRACKET;
    case XKB_KEY_backslash:     return NACK_KEY_BACKSLASH;
    case XKB_KEY_semicolon:     return NACK_KEY_SEMICOLON;
    case XKB_KEY_apostrophe:    return NACK_KEY_APOSTROPHE;
    case XKB_KEY_comma:         return NACK_KEY_COMMA;
    case XKB_KEY_period:        return NACK_KEY_PERIOD;
    case XKB_KEY_slash:         return NACK_KEY_SLASH;
    case XKB_KEY_KP_Enter:      return NACK_KEY_KP_ENTER;
    case XKB_KEY_KP_Divide:     return NACK_KEY_KP_DIVIDE;
    case XKB_KEY_KP_Multiply:   return NACK_KEY_KP_MULTIPLY;
    case XKB_KEY_KP_Subtract:   return NACK_KEY_KP_SUBTRACT;
    case XKB_KEY_KP_Add:        return NACK_KEY_KP_ADD;
    case XKB_KEY_KP_Decimal:    return NACK_KEY_KP_DECIMAL;
    case XKB_KEY_KP_Equal:      return NACK_KEY_KP_EQUAL;
    default: break;
    }

    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z)
        return (nack_key)(NACK_KEY_A + (sym - XKB_KEY_a));
    if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z)
        return (nack_key)(NACK_KEY_A + (sym - XKB_KEY_A));
    if (sym == XKB_KEY_0)
        return NACK_KEY_0;
    if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9)
        return (nack_key)(NACK_KEY_1 + (sym - XKB_KEY_1));
    if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9)
        return sym == XKB_KEY_KP_0 ? NACK_KEY_KP_0
                                   : (nack_key)(NACK_KEY_KP_1 + (sym - XKB_KEY_KP_1));
    /* F1-F12 are contiguous; F13 onwards restarts elsewhere in the HID page. */
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12)
        return (nack_key)(NACK_KEY_F1 + (sym - XKB_KEY_F1));
    if (sym >= XKB_KEY_F13 && sym <= XKB_KEY_F24)
        return (nack_key)(NACK_KEY_F13 + (sym - XKB_KEY_F13));

    return NACK_KEY_UNKNOWN;
}

void nack__xkb_build_keycodes(struct xkb_keymap *keymap, nack_key out[256])
{
    memset(out, 0, sizeof(nack_key) * 256);
    if (!keymap)
        return;

    xkb_keycode_t min = xkb_keymap_min_keycode(keymap);
    xkb_keycode_t max = xkb_keymap_max_keycode(keymap);
    if (max > 255)
        max = 255;

    for (xkb_keycode_t kc = min; kc <= max; ++kc) {
        const char *name = xkb_keymap_key_get_name(keymap, kc);
        if (name) {
            for (size_t i = 0; nack__xkb_names[i].name; ++i) {
                if (strcmp(name, nack__xkb_names[i].name) == 0) {
                    out[kc] = nack__xkb_names[i].key;
                    break;
                }
            }
        }
        if (out[kc] != NACK_KEY_UNKNOWN)
            continue;

        /*
         * No usable name. Derive the key from the unshifted keysym in the
         * first layout instead; that is layout dependent, but a wrong letter
         * beats no key at all, and keymaps without names are synthetic ones
         * where the layout is whatever the sender chose anyway.
         */
        const xkb_keysym_t *syms = nullptr;
        int count = xkb_keymap_key_get_syms_by_level(keymap, kc, 0, 0, &syms);
        if (count > 0 && syms)
            out[kc] = nack__key_from_keysym(syms[0]);
    }
}
