/* Human-readable key names and generic GL helpers. */
#include "nack_internal.h"

static const struct { nack_key key; const char *name; } nack__key_names[] = {
    { NACK_KEY_A, "A" }, { NACK_KEY_B, "B" }, { NACK_KEY_C, "C" },
    { NACK_KEY_D, "D" }, { NACK_KEY_E, "E" }, { NACK_KEY_F, "F" },
    { NACK_KEY_G, "G" }, { NACK_KEY_H, "H" }, { NACK_KEY_I, "I" },
    { NACK_KEY_J, "J" }, { NACK_KEY_K, "K" }, { NACK_KEY_L, "L" },
    { NACK_KEY_M, "M" }, { NACK_KEY_N, "N" }, { NACK_KEY_O, "O" },
    { NACK_KEY_P, "P" }, { NACK_KEY_Q, "Q" }, { NACK_KEY_R, "R" },
    { NACK_KEY_S, "S" }, { NACK_KEY_T, "T" }, { NACK_KEY_U, "U" },
    { NACK_KEY_V, "V" }, { NACK_KEY_W, "W" }, { NACK_KEY_X, "X" },
    { NACK_KEY_Y, "Y" }, { NACK_KEY_Z, "Z" },
    { NACK_KEY_1, "1" }, { NACK_KEY_2, "2" }, { NACK_KEY_3, "3" },
    { NACK_KEY_4, "4" }, { NACK_KEY_5, "5" }, { NACK_KEY_6, "6" },
    { NACK_KEY_7, "7" }, { NACK_KEY_8, "8" }, { NACK_KEY_9, "9" },
    { NACK_KEY_0, "0" },
    { NACK_KEY_ENTER, "Enter" },
    { NACK_KEY_ESCAPE, "Escape" },
    { NACK_KEY_BACKSPACE, "Backspace" },
    { NACK_KEY_TAB, "Tab" },
    { NACK_KEY_SPACE, "Space" },
    { NACK_KEY_MINUS, "Minus" },
    { NACK_KEY_EQUAL, "Equal" },
    { NACK_KEY_LEFT_BRACKET, "LeftBracket" },
    { NACK_KEY_RIGHT_BRACKET, "RightBracket" },
    { NACK_KEY_BACKSLASH, "Backslash" },
    { NACK_KEY_NON_US_HASH, "NonUSHash" },
    { NACK_KEY_SEMICOLON, "Semicolon" },
    { NACK_KEY_APOSTROPHE, "Apostrophe" },
    { NACK_KEY_GRAVE, "Grave" },
    { NACK_KEY_COMMA, "Comma" },
    { NACK_KEY_PERIOD, "Period" },
    { NACK_KEY_SLASH, "Slash" },
    { NACK_KEY_CAPS_LOCK, "CapsLock" },
    { NACK_KEY_F1, "F1" }, { NACK_KEY_F2, "F2" }, { NACK_KEY_F3, "F3" },
    { NACK_KEY_F4, "F4" }, { NACK_KEY_F5, "F5" }, { NACK_KEY_F6, "F6" },
    { NACK_KEY_F7, "F7" }, { NACK_KEY_F8, "F8" }, { NACK_KEY_F9, "F9" },
    { NACK_KEY_F10, "F10" }, { NACK_KEY_F11, "F11" }, { NACK_KEY_F12, "F12" },
    { NACK_KEY_F13, "F13" }, { NACK_KEY_F14, "F14" }, { NACK_KEY_F15, "F15" },
    { NACK_KEY_F16, "F16" }, { NACK_KEY_F17, "F17" }, { NACK_KEY_F18, "F18" },
    { NACK_KEY_F19, "F19" }, { NACK_KEY_F20, "F20" }, { NACK_KEY_F21, "F21" },
    { NACK_KEY_F22, "F22" }, { NACK_KEY_F23, "F23" }, { NACK_KEY_F24, "F24" },
    { NACK_KEY_PRINT_SCREEN, "PrintScreen" },
    { NACK_KEY_SCROLL_LOCK, "ScrollLock" },
    { NACK_KEY_PAUSE, "Pause" },
    { NACK_KEY_INSERT, "Insert" },
    { NACK_KEY_HOME, "Home" },
    { NACK_KEY_PAGE_UP, "PageUp" },
    { NACK_KEY_DELETE, "Delete" },
    { NACK_KEY_END, "End" },
    { NACK_KEY_PAGE_DOWN, "PageDown" },
    { NACK_KEY_RIGHT, "Right" },
    { NACK_KEY_LEFT, "Left" },
    { NACK_KEY_DOWN, "Down" },
    { NACK_KEY_UP, "Up" },
    { NACK_KEY_NUM_LOCK, "NumLock" },
    { NACK_KEY_KP_DIVIDE, "KeypadDivide" },
    { NACK_KEY_KP_MULTIPLY, "KeypadMultiply" },
    { NACK_KEY_KP_SUBTRACT, "KeypadSubtract" },
    { NACK_KEY_KP_ADD, "KeypadAdd" },
    { NACK_KEY_KP_ENTER, "KeypadEnter" },
    { NACK_KEY_KP_1, "Keypad1" }, { NACK_KEY_KP_2, "Keypad2" },
    { NACK_KEY_KP_3, "Keypad3" }, { NACK_KEY_KP_4, "Keypad4" },
    { NACK_KEY_KP_5, "Keypad5" }, { NACK_KEY_KP_6, "Keypad6" },
    { NACK_KEY_KP_7, "Keypad7" }, { NACK_KEY_KP_8, "Keypad8" },
    { NACK_KEY_KP_9, "Keypad9" }, { NACK_KEY_KP_0, "Keypad0" },
    { NACK_KEY_KP_DECIMAL, "KeypadDecimal" },
    { NACK_KEY_KP_EQUAL, "KeypadEqual" },
    { NACK_KEY_NON_US_BACKSLASH, "NonUSBackslash" },
    { NACK_KEY_APPLICATION, "Application" },
    { NACK_KEY_MENU, "Menu" },
    { NACK_KEY_MUTE, "Mute" },
    { NACK_KEY_VOLUME_UP, "VolumeUp" },
    { NACK_KEY_VOLUME_DOWN, "VolumeDown" },
    { NACK_KEY_LEFT_CTRL, "LeftControl" },
    { NACK_KEY_LEFT_SHIFT, "LeftShift" },
    { NACK_KEY_LEFT_ALT, "LeftAlt" },
    { NACK_KEY_LEFT_SUPER, "LeftSuper" },
    { NACK_KEY_RIGHT_CTRL, "RightControl" },
    { NACK_KEY_RIGHT_SHIFT, "RightShift" },
    { NACK_KEY_RIGHT_ALT, "RightAlt" },
    { NACK_KEY_RIGHT_SUPER, "RightSuper" },
};

const char *nack_key_get_name(nack_key key)
{
    static const char *table[NACK_KEY_COUNT];
    static bool built = false;
    if (!built) {
        for (size_t i = 0; i < sizeof nack__key_names / sizeof nack__key_names[0]; ++i)
            table[nack__key_names[i].key] = nack__key_names[i].name;
        built = true;
    }
    if (key <= 0 || key >= NACK_KEY_COUNT || !table[key])
        return "Unknown";
    return table[key];
}

/* ------------------------------------------------------------------ */
/* Generic GL extension query                                         */
/* ------------------------------------------------------------------ */

#define NACK_GL_EXTENSIONS 0x1F03
#define NACK_GL_NUM_EXTENSIONS 0x821D

/* Function pointer types for the entry points resolved below; GLenum is
 * unsigned int in every OpenGL ABI. */
using nack__pfn_get_string = const unsigned char *(*)(unsigned int);
using nack__pfn_get_stringi = const unsigned char *(*)(unsigned int, unsigned int);
using nack__pfn_get_integerv = void (*)(unsigned int, int *);

static bool nack__token_in_list(const char *list, const char *name)
{
    size_t len = strlen(name);
    const char *p = list;
    while (p && *p) {
        const char *found = strstr(p, name);
        if (!found)
            return false;
        char after = found[len];
        if ((found == list || found[-1] == ' ') && (after == ' ' || after == '\0'))
            return true;
        p = found + len;
    }
    return false;
}

bool nack_state::gl_extension_supported(const char *name)
{
    if (!name || !*name || !initialized || !current_context)
        return false;

    nack__pfn_get_stringi get_stringi =
        (nack__pfn_get_stringi)gl_get_proc_address("glGetStringi");
    nack__pfn_get_integerv get_integerv =
        (nack__pfn_get_integerv)gl_get_proc_address("glGetIntegerv");

    if (get_stringi && get_integerv) {
        int count = 0;
        get_integerv(NACK_GL_NUM_EXTENSIONS, &count);
        for (int i = 0; i < count; ++i) {
            const char *ext = (const char *)get_stringi(NACK_GL_EXTENSIONS, (unsigned)i);
            if (ext && strcmp(ext, name) == 0)
                return true;
        }
        if (count > 0)
            return false;
    }

    nack__pfn_get_string get_string =
        (nack__pfn_get_string)gl_get_proc_address("glGetString");
    if (!get_string)
        return false;
    const char *list = (const char *)get_string(NACK_GL_EXTENSIONS);
    return list && nack__token_in_list(list, name);
}
